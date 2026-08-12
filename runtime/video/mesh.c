/* The depth-tested geometry pass. See mesh.h for why it exists and why it is separate. */
#include "mesh.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../shaders/gen/mesh_vert_spv.h"
#include "../shaders/gen/mesh_spv.h"

static SDL_Renderer  *R;
static SDL_GPUDevice *DEV;
static SDL_GPUGraphicsPipeline *PIPE;

/* The offscreen pair, rebuilt when the size changes -- which is every resize, exactly as
 * hd2d.c's lighting chain is. */
static SDL_GPUTexture *tex_color, *tex_depth;
static SDL_Texture    *wrapped;
static int             tex_w, tex_h;

/* A vertex buffer that grows and never shrinks. A stage's geometry is authored once and
 * submitted every frame, so its size is stable after the first frame; reallocating per frame
 * would be the same mistake the surface arena made (vram_alloc has no free). */
static SDL_GPUBuffer *vbuf;
static SDL_GPUTransferBuffer *vxfer;
static int vbuf_cap;

static int  init_done, init_ok;
static const char *init_why = "not attempted";
static long stat_passes, stat_tris;

/* THE LIGHT IS THE SPRITES', not this pass's own. hd2d.c lights the fighters from a direction
 * in the stage's three axes and shears their cast shadows along the SAME vector, so a
 * fighter's shading and their shadow can never disagree. A set lit from somewhere else would
 * put the whole scene into that contradiction one step larger. These are the same numbers;
 * when hd2d.c's become configurable, this reads them rather than keeping a second copy. */
typedef struct { float dir[4], sky[4], ground[4]; } LightUniform;
static const LightUniform LIGHT = {
    { -0.45f, 0.80f, 0.40f, 0.85f },      /* toward the light, and its strength */
    {  0.34f, 0.36f, 0.42f, 0.0f },       /* sky ambient -- cool, from above */
    {  0.20f, 0.18f, 0.16f, 0.0f },       /* ground ambient -- warm, from below */
};

static void selftest(void);

int mesh_ready(void) { return init_ok; }

static SDL_GPUShader *shader_make(const unsigned char *spv, size_t len,
                                  SDL_GPUShaderStage stage, const char *name)
{
    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.code = spv;
    info.code_size = len;
    info.entrypoint = "main";
    info.stage = stage;
    info.num_uniform_buffers = 1;
    SDL_GPUShader *s = SDL_CreateGPUShader(DEV, &info);
    if (!s)
        fprintf(stderr, "mesh: the %s shader could not be created: %s\n", name, SDL_GetError());
    return s;
}

int mesh_init(SDL_Renderer *r)
{
    if (init_done) return init_ok;
    init_done = 1;
    R = r;

    DEV = SDL_GetGPURendererDevice(r);
    if (!DEV) {
        /* Said out loud, and it names the renderer: the usual cause is that SDL chose one that
         * is not the GPU renderer, and a pass that went quiet here would be indistinguishable
         * from a stage that simply has no geometry authored for it. */
        init_why = "the renderer has no GPU device";
        fprintf(stderr, "mesh: the '%s' renderer has no GPU device, so there is no depth "
                        "attachment and stage geometry CANNOT be drawn. Stages fall back to "
                        "their painted layers.\n", SDL_GetRendererName(r));
        return 0;
    }
    if (!(SDL_GetGPUShaderFormats(DEV) & SDL_GPU_SHADERFORMAT_SPIRV)) {
        init_why = "the GPU backend does not take SPIR-V";
        fprintf(stderr, "mesh: the %s backend does not accept SPIR-V, which is the only format "
                        "this port ships -- stage geometry cannot be drawn here.\n",
                SDL_GetGPUDeviceDriver(DEV));
        return 0;
    }
    if (!SDL_GPUTextureSupportsFormat(DEV, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                                      SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        init_why = "no D32_FLOAT depth target";
        fprintf(stderr, "mesh: the %s backend has no D32_FLOAT depth-stencil target, so this "
                        "pass has no depth test and is NOT run -- drawing it without one would "
                        "put the far side of a solid in front of its near side.\n",
                SDL_GetGPUDeviceDriver(DEV));
        return 0;
    }

    SDL_GPUShader *vs = shader_make(mesh_vert_spv, sizeof mesh_vert_spv,
                                    SDL_GPU_SHADERSTAGE_VERTEX, "vertex");
    SDL_GPUShader *fs = shader_make(mesh_spv, sizeof mesh_spv,
                                    SDL_GPU_SHADERSTAGE_FRAGMENT, "fragment");
    if (!vs || !fs) {
        init_why = "a shader failed to compile";
        if (vs) SDL_ReleaseGPUShader(DEV, vs);
        if (fs) SDL_ReleaseGPUShader(DEV, fs);
        return 0;
    }

    SDL_GPUVertexBufferDescription vbd;
    SDL_zero(vbd);
    vbd.slot = 0;
    vbd.pitch = sizeof(MeshVertex);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[3];
    SDL_zero(attrs);
    attrs[0].location = 0; attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attrs[0].offset = (Uint32)offsetof(MeshVertex, x);
    attrs[1].location = 1; attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attrs[1].offset = (Uint32)offsetof(MeshVertex, nx);
    attrs[2].location = 2; attrs[2].buffer_slot = 0;
    attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attrs[2].offset = (Uint32)offsetof(MeshVertex, r);

    SDL_GPUColorTargetDescription ctd;
    SDL_zero(ctd);
    ctd.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    SDL_GPUGraphicsPipelineCreateInfo pi;
    SDL_zero(pi);
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbd;
    pi.vertex_input_state.num_vertex_attributes = 3;
    pi.vertex_input_state.vertex_attributes = attrs;
    pi.target_info.num_color_targets = 1;
    pi.target_info.color_target_descriptions = &ctd;
    pi.target_info.has_depth_stencil_target = true;
    pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    /* LESS, and the clear below is 1.0, so smaller z is nearer. That is the whole point of the
     * pass; a pipeline that reached here with the test disabled would draw a mesh that looks
     * plausible from one angle and inside out from another. */
    pi.depth_stencil_state.enable_depth_test = true;
    pi.depth_stencil_state.enable_depth_write = true;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    /* No back-face culling YET, deliberately: hand-authored geometry with an inconsistent
     * winding would vanish in patches, which reads as a hole in the model rather than as the
     * authoring mistake it is. Turn it on when the format has a validator that can say the
     * winding is consistent. */
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

    PIPE = SDL_CreateGPUGraphicsPipeline(DEV, &pi);
    SDL_ReleaseGPUShader(DEV, vs);
    SDL_ReleaseGPUShader(DEV, fs);
    if (!PIPE) {
        init_why = "the graphics pipeline could not be created";
        fprintf(stderr, "mesh: the depth-tested pipeline could not be created: %s\n",
                SDL_GetError());
        return 0;
    }

    init_ok = 1;
    init_why = "ready";
    fprintf(stderr, "mesh: depth-tested geometry pass ready on the %s backend "
                    "(D32_FLOAT depth, sharing the renderer's device)\n",
            SDL_GetGPUDeviceDriver(DEV));

    /* RUN AT INIT, not from the periodic report. render_report is behind LF2_RENDER_DEBUG and
     * fires every 900 frames, so a self-test called from there needs two switches and a long
     * run to say anything -- which is the same as not having one. Here it fires on frame 0 of
     * any run that asks, and every failure branch above has already returned, so it also
     * cannot be reached in a state where it has nothing to test. */
    if (getenv("LF2_MESH_SELFTEST")) selftest();
    return 1;
}

static void targets_release(void)
{
    if (wrapped)   { SDL_DestroyTexture(wrapped); wrapped = NULL; }
    if (tex_color) { SDL_ReleaseGPUTexture(DEV, tex_color); tex_color = NULL; }
    if (tex_depth) { SDL_ReleaseGPUTexture(DEV, tex_depth); tex_depth = NULL; }
    tex_w = tex_h = 0;
}

static int targets_make(int w, int h)
{
    if (tex_color && tex_w == w && tex_h == h) return 1;
    targets_release();

    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.width = (Uint32)w;
    ci.height = (Uint32)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_color = SDL_CreateGPUTexture(DEV, &ci);

    ci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    tex_depth = SDL_CreateGPUTexture(DEV, &ci);

    if (!tex_color || !tex_depth) {
        fprintf(stderr, "mesh: could not allocate the %dx%d offscreen pair: %s\n",
                w, h, SDL_GetError());
        targets_release();
        return 0;
    }

    /* THE BRIDGE (claim C030): SDL_Render draws the SAME object this pass rendered into --
     * no copy, no readback. */
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, tex_color);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
    wrapped = SDL_CreateTextureWithProperties(R, p);
    SDL_DestroyProperties(p);
    if (!wrapped) {
        fprintf(stderr, "mesh: the colour target could not be wrapped as a texture: %s\n",
                SDL_GetError());
        targets_release();
        return 0;
    }
    SDL_SetTextureScaleMode(wrapped, SDL_SCALEMODE_NEAREST);
    tex_w = w; tex_h = h;
    return 1;
}

static int vbuf_reserve(int n)
{
    const int bytes = n * (int)sizeof(MeshVertex);
    if (vbuf && vbuf_cap >= bytes) return 1;
    if (vbuf)  { SDL_ReleaseGPUBuffer(DEV, vbuf); vbuf = NULL; }
    if (vxfer) { SDL_ReleaseGPUTransferBuffer(DEV, vxfer); vxfer = NULL; }

    SDL_GPUBufferCreateInfo bi;
    SDL_zero(bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = (Uint32)bytes;
    vbuf = SDL_CreateGPUBuffer(DEV, &bi);

    SDL_GPUTransferBufferCreateInfo ti;
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = (Uint32)bytes;
    vxfer = SDL_CreateGPUTransferBuffer(DEV, &ti);

    if (!vbuf || !vxfer) {
        fprintf(stderr, "mesh: could not allocate a %d-byte vertex buffer: %s\n",
                bytes, SDL_GetError());
        if (vbuf)  { SDL_ReleaseGPUBuffer(DEV, vbuf); vbuf = NULL; }
        if (vxfer) { SDL_ReleaseGPUTransferBuffer(DEV, vxfer); vxfer = NULL; }
        vbuf_cap = 0;
        return 0;
    }
    vbuf_cap = bytes;
    return 1;
}

SDL_Texture *mesh_draw(const MeshVertex *v, int n, int w, int h, const float view[16])
{
    if (!init_ok || n <= 0 || w <= 0 || h <= 0) return NULL;
    if (!targets_make(w, h) || !vbuf_reserve(n)) return NULL;

    void *map = SDL_MapGPUTransferBuffer(DEV, vxfer, false);
    if (!map) {
        fprintf(stderr, "mesh: the vertex upload buffer could not be mapped: %s\n",
                SDL_GetError());
        return NULL;
    }
    memcpy(map, v, (size_t)n * sizeof(MeshVertex));
    SDL_UnmapGPUTransferBuffer(DEV, vxfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(DEV);
    if (!cmd) {
        fprintf(stderr, "mesh: no command buffer: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { vxfer, 0 };
    SDL_GPUBufferRegion dst = { vbuf, 0, (Uint32)(n * (int)sizeof(MeshVertex)) };
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.texture = tex_color;
    /* CLEARED TO TRANSPARENT, not to a colour: the display list composites this over the
     * game's own layers, so every texel the geometry does not cover has to let them through.
     * A black clear would put a rectangle over the stage. */
    cti.clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 0.0f };
    cti.load_op = SDL_GPU_LOADOP_CLEAR;
    cti.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = tex_depth;
    dti.clear_depth = 1.0f;                /* the far plane; the test is LESS */
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &cti, 1, &dti);
    if (!pass) {
        fprintf(stderr, "mesh: the render pass could not begin: %s\n", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return NULL;
    }
    SDL_BindGPUGraphicsPipeline(pass, PIPE);
    SDL_GPUBufferBinding vb = { vbuf, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, view, 16 * sizeof(float));
    SDL_PushGPUFragmentUniformData(cmd, 0, &LIGHT, sizeof LIGHT);
    SDL_DrawGPUPrimitives(pass, (Uint32)n, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    stat_passes++;
    stat_tris += n / 3;
    return wrapped;
}

/* ---- the self-test, and it is the reason this file can be believed -------------------------
 *
 * A depth pass that is NOT testing looks perfectly fine on any single frame of a convex model:
 * the far faces are simply painted over by the near ones if they happen to be submitted in
 * that order. So the case that must come out one way is the WRONG order -- the near triangle
 * first, the far one second, overlapping. With the test on, the near one survives; with it off
 * or with the compare reversed, the far one covers it.
 *
 * Both classes are checked here, not reasoned about: the same geometry is also read at a pixel
 * the far triangle covers and the near one does not, which must be the FAR colour. Without that
 * second read, a pass that drew nothing at all would report the same "the near one survived".
 */
static void selftest(void)
{
    if (!init_ok) {
        fprintf(stderr, "mesh selftest: the pass is not available (%s), so NOTHING was "
                        "tested -- this is not a pass\n", init_why);
        return;
    }
    enum { W = 64, H = 64 };
    /* An identity view: the vertices below are already in clip space, so this test is about the
     * depth test and nothing else. */
    static const float IDENT[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

    /* NEAR (z 0.20), red, covering the left half. Submitted FIRST. */
    /* FAR  (z 0.80), blue, covering the whole quad. Submitted SECOND -- so a pass with no
     * depth test paints it over the red. */
    const MeshVertex tri[] = {
        { -1.0f, -1.0f, 0.20f,  0,1,0,  1,0,0,1 },
        {  0.0f, -1.0f, 0.20f,  0,1,0,  1,0,0,1 },
        { -1.0f,  1.0f, 0.20f,  0,1,0,  1,0,0,1 },
        {  0.0f, -1.0f, 0.20f,  0,1,0,  1,0,0,1 },
        {  0.0f,  1.0f, 0.20f,  0,1,0,  1,0,0,1 },
        { -1.0f,  1.0f, 0.20f,  0,1,0,  1,0,0,1 },

        { -1.0f, -1.0f, 0.80f,  0,1,0,  0,0,1,1 },
        {  1.0f, -1.0f, 0.80f,  0,1,0,  0,0,1,1 },
        { -1.0f,  1.0f, 0.80f,  0,1,0,  0,0,1,1 },
        {  1.0f, -1.0f, 0.80f,  0,1,0,  0,0,1,1 },
        {  1.0f,  1.0f, 0.80f,  0,1,0,  0,0,1,1 },
        { -1.0f,  1.0f, 0.80f,  0,1,0,  0,0,1,1 },
    };
    if (!mesh_draw(tri, (int)(sizeof tri / sizeof tri[0]), W, H, IDENT)) {
        fprintf(stderr, "mesh selftest: the pass produced no texture, so NOTHING was tested\n");
        return;
    }

    /* Read the finished target back. This is the one place the pass touches the CPU, and it is
     * a diagnostic path only -- the shipping path never downloads. */
    SDL_GPUTransferBufferCreateInfo ti;
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = W * H * 4;
    SDL_GPUTransferBuffer *dl = SDL_CreateGPUTransferBuffer(DEV, &ti);
    if (!dl) {
        fprintf(stderr, "mesh selftest: no download buffer (%s), so NOTHING was checked\n",
                SDL_GetError());
        return;
    }
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(DEV);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg;
    SDL_zero(reg);
    reg.texture = tex_color; reg.w = W; reg.h = H; reg.d = 1;
    SDL_GPUTextureTransferInfo tti;
    SDL_zero(tti);
    tti.transfer_buffer = dl; tti.pixels_per_row = W; tti.rows_per_layer = H;
    SDL_DownloadFromGPUTexture(copy, &reg, &tti);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) { SDL_WaitForGPUFences(DEV, true, &fence, 1); SDL_ReleaseGPUFence(DEV, fence); }

    const unsigned char *px = SDL_MapGPUTransferBuffer(DEV, dl, false);
    if (!px) {
        fprintf(stderr, "mesh selftest: the download could not be mapped (%s), so NOTHING "
                        "was checked\n", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(DEV, dl);
        return;
    }
    const unsigned char *near_px = px + ((H / 2) * W + W / 4) * 4;   /* left half: both cover */
    const unsigned char *far_px  = px + ((H / 2) * W + 3 * W / 4) * 4; /* right half: far only */
    const int near_is_red  = near_px[0] > near_px[2];
    const int far_is_blue  = far_px[2]  > far_px[0];

    fprintf(stderr, "mesh selftest: overlap pixel rgb(%u,%u,%u) -- the NEAR triangle %s, "
                    "though it was submitted FIRST and the far one over it\n",
            near_px[0], near_px[1], near_px[2],
            near_is_red ? "SURVIVED, so the depth test is running"
                        : "WAS COVERED, so the depth test is NOT running");
    fprintf(stderr, "mesh selftest: far-only pixel rgb(%u,%u,%u) -- the far triangle %s, which "
                    "is what rules out a pass that simply drew nothing\n",
            far_px[0], far_px[1], far_px[2],
            far_is_blue ? "DID draw" : "did NOT draw");
    fprintf(stderr, "mesh selftest: %s\n",
            (near_is_red && far_is_blue) ? "PASS" : "FAIL");

    SDL_UnmapGPUTransferBuffer(DEV, dl);
    SDL_ReleaseGPUTransferBuffer(DEV, dl);
}

void mesh_report(void)
{
    /* The self-test is not here -- it runs at init; see mesh_init. What is left is the
     * accounting, and it says the zero case out loud. */
    if (!init_ok) {
        fprintf(stderr, "mesh: the geometry pass never ran -- %s\n", init_why);
        return;
    }
    fprintf(stderr, "mesh: %ld pass(es), %ld triangle(s)%s\n", stat_passes, stat_tris,
            stat_passes ? "" : " -- no stage submitted any geometry, which is every stage "
                               "until one is authored (issue #62)");
}
