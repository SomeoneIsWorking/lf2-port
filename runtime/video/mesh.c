/* The depth-tested geometry pass. See mesh.h for why it exists and why it is separate. */
#include "mesh.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "hd2d.h"
#include "gpu_depth_format.h"
#include "gpu_shader_source.h"

#include "../shaders/gen/mesh_vert_spv.h"
#include "../shaders/gen/mesh_spv.h"
#include "../shaders/gen/mesh_vert_msl.h"
#include "../shaders/gen/mesh_msl.h"

static SDL_Renderer  *R;
static SDL_GPUDevice *DEV;
static SDL_GPUGraphicsPipeline *PIPE;
static SDL_GPUShaderFormat SHADER_FORMATS;
static SDL_GPUTextureFormat DEPTH_FORMAT;

/* The offscreen pair, rebuilt when the size changes -- which is every resize, exactly as
 * hd2d.c's lighting chain is. */
/* ONE TARGET PER SLOT, and the reason there is more than one is the painter order (issue #62).
 *
 * A finished pass is composited as a single full-screen quad, and a single quad goes into the
 * game's painter order at a single point -- but a hand-woven set spans parallax depths and the
 * game paints its OWN layers between them. The Great Wall's `road3` is in front of the fighters
 * while its `sky` is 267 deep, so a set with a far pillar and a near railing has layers that
 * belong BETWEEN its two solids. "Behind every layer" and "in front of every layer" are both
 * wrong for it.
 *
 * So the caller runs the pass once per OCCUPIED GAP in the layer order, and each of those needs
 * its own live target -- a single target would be overwritten by the next gap before the frame
 * was drawn. Slots are allocated on demand: a stage using three gaps costs three, and a stage
 * with no geometry costs none. MESH_SLOTS bounds it, and going over is REPORTED and refused
 * rather than quietly merged into a neighbouring slot, which would draw the solid at the wrong
 * point in the order and look like geometry rather than like a bug. */
enum { MESH_SLOTS = 8 };
typedef struct {
    SDL_GPUTexture *color, *depth;
    SDL_Texture    *wrapped;
    int             w, h;
} MeshTarget;
static MeshTarget slots[MESH_SLOTS];

/* A vertex buffer that grows and never shrinks. A stage's geometry is authored once and
 * submitted every frame, so its size is stable after the first frame; reallocating per frame
 * would be the same mistake the surface arena made (vram_alloc has no free). */
static SDL_GPUBuffer *vbuf;
static SDL_GPUTransferBuffer *vxfer;
static int vbuf_cap;

static int  init_done, init_ok;
static const char *init_why = "not attempted";
static long stat_passes, stat_tris;
static int  stat_slots;   /* the most slots any one frame has needed */

/* THE LIGHT IS THE SPRITES', not this pass's own, and it is now READ rather than copied.
 *
 * hd2d.c lights the fighters from a direction in the stage's three axes and shears their cast
 * shadows along the SAME vector, so a fighter's shading and their shadow can never disagree. A
 * set lit from somewhere else puts the whole scene into that contradiction one step larger --
 * geometry lit from the right while every shadow in the picture falls to the left.
 *
 * THIS FILE USED TO HOLD A COPY, and the copy had already drifted. It said { -0.45, 0.80, 0.40 }
 * where hd2d.c says { -0.25, 0.94, 0.22 }, with a comment asserting "these are the same
 * numbers" -- so the two were about 15 degrees apart and the note said they were not. Worse,
 * hd2d.c's is not a constant at all: the pause menu's Options screen sets it from two angles
 * (issue #37), so a player moving the light moved the fighters and left the set behind.
 *
 * Nothing to keep in step now. hd2d_light_vector() is the single source, read per draw, which
 * is once per gap per frame and not a cost worth caching. */
typedef struct { float dir[4], sky[4], ground[4], tint[4]; } LightUniform;
static LightUniform light_now(void)
{
    LightUniform u = {
        { 0.0f, 1.0f, 0.0f, 0.85f },      /* dir.xyz from hd2d below; .w is this pass's key */
        { 0.34f, 0.36f, 0.42f, 0.0f },    /* sky ambient -- cool, from above */
        { 0.20f, 0.18f, 0.16f, 0.0f },    /* ground ambient -- warm, from below */
        { 0.0f,  0.0f,  0.0f,  0.0f },    /* tint.x set per draw: 1 with a texture, 0 without */
    };
    float d[3];
    hd2d_light_vector(d);
    /* A zero vector would be hd2d never having been initialised; the default above stands
     * rather than normalising a zero and shading everything black. */
    if (d[0] != 0.0f || d[1] != 0.0f || d[2] != 0.0f) {
        u.dir[0] = d[0]; u.dir[1] = d[1]; u.dir[2] = d[2];
    }
    return u;
}

static SDL_GPUSampler *SMP;


static void selftest(void);
static void selftest_texture(void);

int mesh_ready(void) { return init_ok; }

static SDL_GPUShader *shader_make(const unsigned char *spv, size_t spv_len,
                                  const unsigned char *msl, size_t msl_len,
                                  SDL_GPUShaderStage stage, const char *name)
{
    GPUShaderSource source;
    if (!gpu_shader_source_select(SHADER_FORMATS, spv, spv_len, msl, msl_len, &source)) {
        fprintf(stderr, "mesh: no shader payload matches the %s backend for %s\n",
                SDL_GetGPUDeviceDriver(DEV), name);
        return NULL;
    }
    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.format = source.format;
    info.code = source.code;
    info.code_size = source.code_size;
    info.entrypoint = source.entrypoint;
    info.stage = stage;
    info.num_uniform_buffers = 1;
    /* The fragment stage takes the source texture. Declared even when a draw binds none: the
     * shader multiplies by u_tint.x, so the untextured case is a multiply by white rather than
     * a second pipeline to keep in step with this one. */
    info.num_samplers = (stage == SDL_GPU_SHADERSTAGE_FRAGMENT) ? 1u : 0u;
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
    SHADER_FORMATS = SDL_GetGPUShaderFormats(DEV);
    if (!(SHADER_FORMATS & (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL))) {
        init_why = "the GPU backend has no matching shader payload";
        fprintf(stderr, "mesh: the %s backend accepts shader formats 0x%x, but this port ships "
                        "SPIR-V and MSL -- stage geometry cannot be drawn here.\n",
                SDL_GetGPUDeviceDriver(DEV), (unsigned)SHADER_FORMATS);
        return 0;
    }
    DEPTH_FORMAT = gpu_depth_format_select(DEV);
    if (DEPTH_FORMAT == SDL_GPU_TEXTUREFORMAT_INVALID) {
        init_why = "no supported depth target";
        fprintf(stderr, "mesh: the %s backend has no supported depth-stencil target, so this "
                        "pass has no depth test and is NOT run -- drawing it without one would "
                        "put the far side of a solid in front of its near side.\n",
                SDL_GetGPUDeviceDriver(DEV));
        return 0;
    }

    SDL_GPUShader *vs = shader_make(mesh_vert_spv, sizeof mesh_vert_spv,
                                    mesh_vert_msl, sizeof mesh_vert_msl,
                                    SDL_GPU_SHADERSTAGE_VERTEX, "vertex");
    SDL_GPUShader *fs = shader_make(mesh_spv, sizeof mesh_spv,
                                    mesh_msl, sizeof mesh_msl,
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

    SDL_GPUVertexAttribute attrs[4];
    SDL_zero(attrs);
    /* FOUR floats: x, jump, row, depth. See MeshVertex in mesh.h for why row and depth are
     * separate channels and must stay that way. */
    attrs[0].location = 0; attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attrs[0].offset = (Uint32)offsetof(MeshVertex, x);
    attrs[1].location = 1; attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[1].offset = (Uint32)offsetof(MeshVertex, u);
    attrs[2].location = 2; attrs[2].buffer_slot = 0;
    attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attrs[2].offset = (Uint32)offsetof(MeshVertex, nx);
    attrs[3].location = 3; attrs[3].buffer_slot = 0;
    attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attrs[3].offset = (Uint32)offsetof(MeshVertex, r);

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
    pi.vertex_input_state.num_vertex_attributes = 4;
    pi.vertex_input_state.vertex_attributes = attrs;
    pi.target_info.num_color_targets = 1;
    pi.target_info.color_target_descriptions = &ctd;
    pi.target_info.has_depth_stencil_target = true;
    pi.target_info.depth_stencil_format = DEPTH_FORMAT;
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

    /* NEAREST, like everything else the port draws (issue #41): the art is pixel art and a
     * linear filter on a magnified texel is the blur that scaling was removed to avoid. */
    SDL_GPUSamplerCreateInfo si;
    SDL_zero(si);
    si.min_filter = SDL_GPU_FILTER_NEAREST;
    si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SMP = SDL_CreateGPUSampler(DEV, &si);
    if (!SMP) {
        init_why = "the sampler could not be created";
        fprintf(stderr, "mesh: the sampler could not be created: %s\n", SDL_GetError());
        return 0;
    }

    init_ok = 1;
    init_why = "ready";
    GPUShaderSource selected;
    (void)gpu_shader_source_select(SHADER_FORMATS, mesh_vert_spv, sizeof mesh_vert_spv,
                                   mesh_vert_msl, sizeof mesh_vert_msl, &selected);
    fprintf(stderr, "mesh: depth-tested geometry pass ready on the %s backend with %s shaders "
                    "(%s depth, sharing the renderer's device)\n",
            SDL_GetGPUDeviceDriver(DEV), gpu_shader_format_name(selected.format),
            gpu_depth_format_name(DEPTH_FORMAT));

    /* RUN AT INIT, not from the periodic report. render_report is behind LF2_RENDER_DEBUG and
     * fires every 900 frames, so a self-test called from there needs two switches and a long
     * run to say anything -- which is the same as not having one. Here it fires on frame 0 of
     * any run that asks, and every failure branch above has already returned, so it also
     * cannot be reached in a state where it has nothing to test. */
    if (getenv("LF2_MESH_SELFTEST")) selftest();
    return 1;
}

static void targets_release(MeshTarget *t)
{
    if (t->wrapped) { SDL_DestroyTexture(t->wrapped); t->wrapped = NULL; }
    if (t->color)   { SDL_ReleaseGPUTexture(DEV, t->color); t->color = NULL; }
    if (t->depth)   { SDL_ReleaseGPUTexture(DEV, t->depth); t->depth = NULL; }
    t->w = t->h = 0;
}

static int targets_make(MeshTarget *t, int w, int h)
{
    if (t->color && t->w == w && t->h == h) return 1;
    targets_release(t);

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
    t->color = SDL_CreateGPUTexture(DEV, &ci);

    ci.format = DEPTH_FORMAT;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    t->depth = SDL_CreateGPUTexture(DEV, &ci);

    if (!t->color || !t->depth) {
        fprintf(stderr, "mesh: could not allocate the %dx%d offscreen pair: %s\n",
                w, h, SDL_GetError());
        targets_release(t);
        return 0;
    }

    /* THE BRIDGE (claim C030): SDL_Render draws the SAME object this pass rendered into --
     * no copy, no readback. */
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, t->color);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
    t->wrapped = SDL_CreateTextureWithProperties(R, p);
    SDL_DestroyProperties(p);
    if (!t->wrapped) {
        fprintf(stderr, "mesh: the colour target could not be wrapped as a texture: %s\n",
                SDL_GetError());
        targets_release(t);
        return 0;
    }
    SDL_SetTextureScaleMode(t->wrapped, SDL_SCALEMODE_NEAREST);
    t->w = w; t->h = h;
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

struct MeshTexture { SDL_GPUTexture *tex; int w, h; };

/* The pass's own upload. See mesh.h for why the art is on the GPU twice: reaching into the
 * texture render.c already uploaded was measured and does not work. */
MeshTexture *mesh_upload(const void *rgba, int w, int h)
{
    if (!init_ok || !rgba || w <= 0 || h <= 0) return NULL;

    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.width = (Uint32)w; ci.height = (Uint32)h;
    ci.layer_count_or_depth = 1; ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(DEV, &ci);

    SDL_GPUTransferBufferCreateInfo ti;
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = (Uint32)(w * h * 4);
    SDL_GPUTransferBuffer *tb = tex ? SDL_CreateGPUTransferBuffer(DEV, &ti) : NULL;
    void *map = tb ? SDL_MapGPUTransferBuffer(DEV, tb, false) : NULL;
    if (!map) {
        fprintf(stderr, "mesh: a %dx%d texture upload failed: %s\n", w, h, SDL_GetError());
        if (tb) SDL_ReleaseGPUTransferBuffer(DEV, tb);
        if (tex) SDL_ReleaseGPUTexture(DEV, tex);
        return NULL;
    }
    memcpy(map, rgba, (size_t)w * (size_t)h * 4u);
    SDL_UnmapGPUTransferBuffer(DEV, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(DEV);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = { tb, 0, (Uint32)w, (Uint32)h };
    SDL_GPUTextureRegion dst;
    SDL_zero(dst);
    dst.texture = tex; dst.w = (Uint32)w; dst.h = (Uint32)h; dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    /* Waited on: the very next thing a caller does is draw with it, and an upload still in
     * flight samples as the zeros this whole redesign came from. */
    SDL_GPUFence *f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (f) { SDL_WaitForGPUFences(DEV, true, &f, 1); SDL_ReleaseGPUFence(DEV, f); }
    SDL_ReleaseGPUTransferBuffer(DEV, tb);

    MeshTexture *m = (MeshTexture *)SDL_calloc(1, sizeof *m);
    if (!m) { SDL_ReleaseGPUTexture(DEV, tex); return NULL; }
    m->tex = tex; m->w = w; m->h = h;
    return m;
}

void mesh_texture_free(MeshTexture *t)
{
    if (!t) return;
    if (t->tex && DEV) SDL_ReleaseGPUTexture(DEV, t->tex);
    SDL_free(t);
}

SDL_Texture *mesh_draw(int slot, const MeshVertex *v, int n, int w, int h,
                       int camera, int view_w, int view_h, const MeshTexture *art)
{
    if (!init_ok || n <= 0 || w <= 0 || h <= 0) return NULL;
    if (slot < 0 || slot >= MESH_SLOTS) {
        /* SAID, not clamped. Clamping would draw this geometry into another gap's target and
         * put it at the wrong point in the painter order -- a solid in front of a layer it
         * belongs behind, which reads as a set that was authored badly rather than as a pass
         * that ran out of slots. Once per process: it is a property of the stage, not of the
         * frame, so a per-frame message would bury everything else. */
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "mesh: this stage's geometry needs slot %d and the pass holds %d "
                            "-- that many separate places in the layer order. The solids in "
                            "the deeper gaps are NOT drawn; raise MESH_SLOTS or merge solids "
                            "onto fewer parallax depths\n", slot, MESH_SLOTS);
        }
        return NULL;
    }
    MeshTarget *t = &slots[slot];
    if (!targets_make(t, w, h) || !vbuf_reserve(n)) return NULL;

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
    SDL_GPUTransferBufferLocation from = { vxfer, 0 };
    SDL_GPUBufferRegion dst = { vbuf, 0, (Uint32)(n * (int)sizeof(MeshVertex)) };
    SDL_UploadToGPUBuffer(copy, &from, &dst, false);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.texture = t->color;
    /* CLEARED TO TRANSPARENT, not to a colour: the display list composites this over the
     * game's own layers, so every texel the geometry does not cover has to let them through.
     * A black clear would put a rectangle over the stage. */
    cti.clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 0.0f };
    cti.load_op = SDL_GPU_LOADOP_CLEAR;
    cti.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = t->depth;
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

    /* The pass's OWN texture. Claim C032 said this could be SDL's, and it is falsified: the
     * handle reads back fine and a sample through it comes out rgba(0,0,0,0). See mesh.h.
     *
     * A sampler must be bound whether or not there is art: the fragment shader declares one,
     * and leaving it unbound is undefined rather than "the branch is not taken". The colour
     * target stands in when there is nothing to sample -- u_tint.x is 0, so nothing reads it. */
    LightUniform lu = light_now();
    if (art && art->tex) lu.tint[0] = 1.0f;
    SDL_GPUTextureSamplerBinding tsb = { (art && art->tex) ? art->tex : t->color, SMP };
    SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
    /* The camera and the view, which is all the projection needs -- there is no matrix,
     * because screen_x = X - camera/depth is not linear in (X, depth, 1). See geom.h. */
    /* This pass owns its whole target, so the placement is the identity map -- stage pixel 0
     * is the left edge, view_w is the right -- and the geometry spans the whole depth buffer.
     * The engine passes a real placement and a narrower sliver; see engine.c and mesh.vert. */
    const float camv[12] = {
        (float)camera, 0.0f, 0.0f, 0.0f,
        2.0f / (float)view_w, -1.0f, -2.0f / (float)view_h, 1.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
    };
    SDL_PushGPUVertexUniformData(cmd, 0, camv, sizeof camv);
    SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
    SDL_DrawGPUPrimitives(pass, (Uint32)n, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    stat_passes++;
    stat_tris += n / 3;
    if (slot + 1 > stat_slots) stat_slots = slot + 1;
    return t->wrapped;
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
    enum { W = 64, H = 64, VIEW_W = 64, VIEW_H = 64 };

    /* The camera is 0 so the horizontal shift is out of the picture: this test is about the
     * DEPTH TEST and nothing else, and a projection bug would otherwise be able to move a
     * triangle off the pixels being read and look like a depth failure.
     *
     * NEAR is depth 1.0 (the fighters' plane) and covers the LEFT half. It is submitted FIRST.
     * FAR is depth 9.0 and covers the WHOLE quad, submitted SECOND -- so a pass with no depth
     * test paints it over the near one. */
    const MeshVertex tri[] = {
        {  0.0f, 0.0f,  0.0f, 1.0f,  0,0,  0,1,0,  1,0,0,1 },
        { 32.0f, 0.0f,  0.0f, 1.0f,  0,0,  0,1,0,  1,0,0,1 },
        {  0.0f, 0.0f, 64.0f, 1.0f,  0,0,  0,1,0,  1,0,0,1 },
        { 32.0f, 0.0f,  0.0f, 1.0f,  0,0,  0,1,0,  1,0,0,1 },
        { 32.0f, 0.0f, 64.0f, 1.0f,  0,0,  0,1,0,  1,0,0,1 },
        {  0.0f, 0.0f, 64.0f, 1.0f,  0,0,  0,1,0,  1,0,0,1 },

        {  0.0f, 0.0f,  0.0f, 9.0f,  0,0,  0,1,0,  0,0,1,1 },
        { 64.0f, 0.0f,  0.0f, 9.0f,  0,0,  0,1,0,  0,0,1,1 },
        {  0.0f, 0.0f, 64.0f, 9.0f,  0,0,  0,1,0,  0,0,1,1 },
        { 64.0f, 0.0f,  0.0f, 9.0f,  0,0,  0,1,0,  0,0,1,1 },
        { 64.0f, 0.0f, 64.0f, 9.0f,  0,0,  0,1,0,  0,0,1,1 },
        {  0.0f, 0.0f, 64.0f, 9.0f,  0,0,  0,1,0,  0,0,1,1 },
    };
    if (!mesh_draw(0, tri, (int)(sizeof tri / sizeof tri[0]), W, H, 0, VIEW_W, VIEW_H, NULL)) {
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
    reg.texture = slots[0].color; reg.w = W; reg.h = H; reg.d = 1;
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

    selftest_texture();
}

/* A pixel out of the finished colour target. Diagnostic only -- the shipping path never reads
 * back -- and it returns 0 rather than a colour when it could not, so a failure to read cannot
 * be mistaken for a black pixel. */
static int readback_px(int w, int h, int x, int y, unsigned char out[4])
{
    SDL_GPUTransferBufferCreateInfo ti;
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = (Uint32)(w * h * 4);
    SDL_GPUTransferBuffer *dl = SDL_CreateGPUTransferBuffer(DEV, &ti);
    if (!dl) return 0;

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(DEV);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg;
    SDL_zero(reg);
    reg.texture = slots[0].color; reg.w = (Uint32)w; reg.h = (Uint32)h; reg.d = 1;
    SDL_GPUTextureTransferInfo tti;
    SDL_zero(tti);
    tti.transfer_buffer = dl; tti.pixels_per_row = (Uint32)w; tti.rows_per_layer = (Uint32)h;
    SDL_DownloadFromGPUTexture(copy, &reg, &tti);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (f) { SDL_WaitForGPUFences(DEV, true, &f, 1); SDL_ReleaseGPUFence(DEV, f); }

    const unsigned char *px = SDL_MapGPUTransferBuffer(DEV, dl, false);
    int ok = 0;
    if (px) {
        const unsigned char *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4u;
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
        ok = 1;
        SDL_UnmapGPUTransferBuffer(DEV, dl);
    }
    SDL_ReleaseGPUTransferBuffer(DEV, dl);
    return ok;
}

/* ---- and the TEXTURE path, which the depth test above does not touch ------------------------
 *
 * Untested sampling fails in ways that look like something else: an unbound sampler draws
 * black and reads as "the geometry is not there", a wrong UV draws one texel across the whole
 * quad and reads as flat shading. Neither announces itself.
 *
 * This draws a quad whose source has two differently coloured halves and reads a pixel in each.
 * Both must come back their own colour: the same colour in both means the UVs are not reaching
 * the sampler, and the clear value in both means nothing was sampled.
 *
 * AND THE UNTEXTURED CONTROL IS NOT OPTIONAL. It earned its place: the first cut of this test
 * came back rgba(0,0,0,0), which is indistinguishable between "the sample failed" and "the quad
 * never rasterised". The control -- the SAME quad with no art, which must read white -- is what
 * separated them in one run. It is also what found the real fault: sampling SDL's own texture
 * through its GPU handle gives zeros, which is why claim C032 is falsified and why this pass
 * owns its uploads.
 */
static void selftest_texture(void)
{
    enum { W = 64, H = 64, TW = 16, TH = 4 };

    unsigned char px[TW * TH * 4];
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            unsigned char *p = px + (y * TW + x) * 4;
            const int left = x < TW / 2;
            p[0] = (unsigned char)(left ? 255 : 0);
            p[1] = (unsigned char)(left ? 0 : 255);
            p[2] = 0;
            p[3] = 255;
        }

    /* One quad over the whole view, u running 0..1 across it. Depth 1.0 and camera 0, so the
     * projection is out of the picture and this is about sampling only. */
    const MeshVertex quad[] = {
        {  0.0f, 0.0f,  0.0f, 1.0f,  0.0f, 0.0f,  0,1,0,  1,1,1,1 },
        { 64.0f, 0.0f,  0.0f, 1.0f,  1.0f, 0.0f,  0,1,0,  1,1,1,1 },
        {  0.0f, 0.0f, 64.0f, 1.0f,  0.0f, 1.0f,  0,1,0,  1,1,1,1 },
        { 64.0f, 0.0f,  0.0f, 1.0f,  1.0f, 0.0f,  0,1,0,  1,1,1,1 },
        { 64.0f, 0.0f, 64.0f, 1.0f,  1.0f, 1.0f,  0,1,0,  1,1,1,1 },
        {  0.0f, 0.0f, 64.0f, 1.0f,  0.0f, 1.0f,  0,1,0,  1,1,1,1 },
    };
    const int NV = (int)(sizeof quad / sizeof quad[0]);

    if (mesh_draw(0, quad, NV, W, H, 0, W, H, NULL)) {
        unsigned char c[4] = { 0, 0, 0, 0 };
        if (readback_px(W, H, W / 2, H / 2, c))
            fprintf(stderr, "mesh selftest: untextured control rgba(%u,%u,%u,%u) -- the quad "
                            "%s\n", c[0], c[1], c[2], c[3],
                    c[3] ? "rasterises, so anything blank below is the SAMPLE and not the "
                           "geometry"
                         : "did NOT rasterise, so nothing below is about texturing");
    }

    MeshTexture *art = mesh_upload(px, TW, TH);
    if (!art) {
        fprintf(stderr, "mesh selftest: the art could not be uploaded, so the TEXTURE path was "
                        "NOT tested\n");
        return;
    }
    if (!mesh_draw(0, quad, NV, W, H, 0, W, H, art)) {
        fprintf(stderr, "mesh selftest: the textured draw produced no texture, so the TEXTURE "
                        "path was NOT tested\n");
        mesh_texture_free(art);
        return;
    }

    unsigned char l[4] = { 0, 0, 0, 0 }, r[4] = { 0, 0, 0, 0 };
    if (!readback_px(W, H, W / 4, H / 2, l) || !readback_px(W, H, 3 * W / 4, H / 2, r)) {
        fprintf(stderr, "mesh selftest: the texture arm could not be read back, so it measured "
                        "NOTHING\n");
        mesh_texture_free(art);
        return;
    }
    const int left_red    = l[0] > l[1] && l[0] > 40;
    const int right_green = r[1] > r[0] && r[1] > 40;
    fprintf(stderr, "mesh selftest: textured quad reads rgba(%u,%u,%u,%u) on the left and "
                    "rgba(%u,%u,%u,%u) on the right -- the source's own two halves %s\n",
            l[0], l[1], l[2], l[3], r[0], r[1], r[2], r[3],
            (left_red && right_green)
                ? "arrived in the right places, so the sampler and the UVs are live"
                : "did NOT arrive: the same colour both sides means the UVs are not reaching "
                  "the sampler, the clear value means nothing was sampled");
    fprintf(stderr, "mesh selftest: TEXTURE %s\n", (left_red && right_green) ? "PASS" : "FAIL");
    mesh_texture_free(art);
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
