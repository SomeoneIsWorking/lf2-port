/* The RmlUi settings screen (issue #70). See rmlui.h for what this is and why it is C++.
 *
 * The document and its style are embedded here, font included (the port's committed
 * Liberation face, the same one its own text is drawn with), so the screen has no asset path
 * to find and no font the host happened to install. The build needs the C++ compiler, RmlUi
 * (third_party/RmlUi) and FreeType; everything else in this port is C and stays so.
 *
 * RMLUI_SDL_VERSION_MAJOR is set by CMakeLists.txt (third-party/RmlUi/Backends expects it).
 */
#include "rmlui.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <SDL3/SDL.h>

#include "RmlUi_Platform_SDL.h"

extern "C" {
#include "config.h"
#include "hostwin.h"
#include "options.h"
}

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

/* The port's committed font face, compiled in by CMakeLists.txt. */
extern const unsigned char lf2_font_sans[];
extern const unsigned int  lf2_font_sans_len;

/* ---- the document, embedded ----
 *
 * The settings screen is small and its data few, so the whole thing -- markup, style, the
 * data-model hookup -- is one string. `data-model="settings"` binds the body to the model the
 * glue constructs below; the checkboxes write the engine/lighting/dof booleans back to
 * options.c every frame the screen is up, and each key row shows the config's current binding
 * and starts a rebind when clicked (its next key press is the binding).
 */
static const char SETTINGS_RML[] = R"RML(
<rml>
<head>
<style>
  body {
    width: 100%; height: 100%;
    background: transparent;
    font-family: lf2;
    font-size: 16px;
  }
  #panel {
    width: 430px;
    margin: 70px auto 0 auto;
    background: #203050;
    border: 2px #6080b0;
    padding: 12px 14px;
    color: #b0c0d0;
  }
  #title { color: #ffffff; text-align: center; font-size: 18px; margin-bottom: 8px; }
  .group { color: #80a0d0; margin-top: 12px; }
  .row { margin-top: 6px; }
  .row .label { display: inline-block; width: 190px; }
  .row .key {
    display: inline-block; width: 100px; text-align: center;
    background: #102040; color: #e0e8f0; padding: 2px 0;
  }
  .row .key.capture { background: #a05030; color: #fff; }
  input[type=checkbox] { width: 18px; height: 18px; }
  #close {
    text-align: center; margin-top: 16px; padding: 4px 0;
    background: #4870a0; color: #ffffff;
  }
</style>
</head>
<body data-model="settings">
  <div id="panel">
    <div id="title">SETTINGS</div>
    <div class="group">GRAPHICS</div>
    <div class="row"><span class="label">Render engine</span><input type="checkbox" data-bind="checked: engine"/></div>
    <div class="row"><span class="label">Lighting</span><input type="checkbox" data-bind="checked: lighting"/></div>
    <div class="row"><span class="label">Depth of field</span><input type="checkbox" data-bind="checked: dof"/></div>
    <div class="group">CONTROLS</div>
    <div class="row"><span class="label">Up</span><span class="key" data-event-click="capture('up')"><span data-bind="text: key_up"/></span></div>
    <div class="row"><span class="label">Down</span><span class="key" data-event-click="capture('down')"><span data-bind="text: key_down"/></span></div>
    <div class="row"><span class="label">Left</span><span class="key" data-event-click="capture('left')"><span data-bind="text: key_left"/></span></div>
    <div class="row"><span class="label">Right</span><span class="key" data-event-click="capture('right')"><span data-bind="text: key_right"/></span></div>
    <div class="row"><span class="label">Attack</span><span class="key" data-event-click="capture('attack')"><span data-bind="text: key_attack"/></span></div>
    <div class="row"><span class="label">Jump</span><span class="key" data-event-click="capture('jump')"><span data-bind="text: key_jump"/></span></div>
    <div class="row"><span class="label">Defend</span><span class="key" data-event-click="capture('defend')"><span data-bind="text: key_defend"/></span></div>
    <div id="close" data-event-click="close">CLOSE</div>
  </div>
</body>
</rml>
)RML";

/* The seven buttons in the game's own order (config.h), as the document names them. */
static const char *const BTN_ID[B_N] = {
    "up", "down", "left", "right", "attack", "jump", "defend",
};

/* ---- the render interface ----
 *
 * RmlUi's stock SDL_Renderer backend (Backends/RmlUi_Renderer_SDL.cpp) pulls in SDL3_image
 * for its file-based LoadTexture, which this port neither has nor needs (the settings UI has
 * no <img>). This is that backend's geometry and texture half written small, without the
 * image dependency. Two deliberate differences:
 *
 *   BeginFrame does NOT clear -- the document COMPOSITES over the frozen frame that already
 *   sits in the render target, and clearing would wipe the picture it is drawn on top of.
 *
 *   The vertices are converted to SDL_Vertex at compile time (RmlUi's Colourb -> the float
 *   colour SDL_RenderGeometry expects), so RenderGeometry only applies the translation. */
class SettingsRenderInterface : public Rml::RenderInterface {
public:
    explicit SettingsRenderInterface(SDL_Renderer *renderer) : renderer(renderer)
    {
        /* RmlUi serves premultiplied-alpha colours and textures, so the blend is
         * (ONE, ONE_MINUS_SRC_ALPHA) -- the same pairing RmlUi's own backend sets. */
        blend = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                           SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE,
                                           SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    }

    /* Not part of RmlUi's RenderInterface -- this glue's own frame hooks, called from
     * rmlui_render. BeginFrame must not clear: the document composites over the frozen frame
     * that already sits in the render target. */
    void BeginFrame() { SDL_SetRenderDrawBlendMode(renderer, blend); }
    void EndFrame() {}

    struct Geometry {
        std::unique_ptr<SDL_Vertex[]> verts;
        int num_verts = 0;
        std::unique_ptr<int[]> indices;
        int num_idx = 0;
    };

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override
    {
        auto g = std::make_unique<Geometry>();
        g->num_verts = (int)vertices.size();
        g->verts.reset(new SDL_Vertex[g->num_verts]);
        for (int i = 0; i < g->num_verts; i++) {
            const Rml::Vertex &v = vertices[i];
            g->verts[i].position = { v.position.x, v.position.y };
            g->verts[i].tex_coord = { v.tex_coord.x, v.tex_coord.y };
            g->verts[i].color = { v.colour.red / 255.f, v.colour.green / 255.f,
                                  v.colour.blue / 255.f, v.colour.alpha / 255.f };
        }
        g->num_idx = (int)indices.size();
        g->indices.reset(new int[g->num_idx]);
        std::memcpy(g->indices.get(), indices.data(), (size_t)g->num_idx * sizeof(int));
        return reinterpret_cast<Rml::CompiledGeometryHandle>(g.release());
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle h) override
    {
        delete reinterpret_cast<Geometry *>(h);
    }

    void RenderGeometry(Rml::CompiledGeometryHandle h, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override
    {
        Geometry *g = reinterpret_cast<Geometry *>(h);
        std::unique_ptr<SDL_Vertex[]> v(new SDL_Vertex[g->num_verts]);
        for (int i = 0; i < g->num_verts; i++) {
            v[i] = g->verts[i];
            v[i].position.x += translation.x;
            v[i].position.y += translation.y;
        }
        SDL_RenderGeometry(renderer, reinterpret_cast<SDL_Texture *>(texture), v.get(),
                           g->num_verts, g->indices.get(), g->num_idx);
    }

    /* No <img> in the settings UI: the only textures RmlUi asks for are glyph atlases, which
     * come through GenerateTexture. A file-based image would be a new asset path this port
     * does not have, so it is refused rather than silently missing. */
    Rml::TextureHandle LoadTexture(Rml::Vector2i &, const Rml::String &) override { return 0; }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i dim) override
    {
        SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STATIC, dim.x, dim.y);
        if (!tex) return 0;
        SDL_SetTextureBlendMode(tex, blend);
        if (SDL_UpdateTexture(tex, nullptr, source.data(), dim.x * 4) != 0) {
            SDL_DestroyTexture(tex);
            return 0;
        }
        return reinterpret_cast<Rml::TextureHandle>(tex);
    }

    void ReleaseTexture(Rml::TextureHandle h) override
    {
        if (h) SDL_DestroyTexture(reinterpret_cast<SDL_Texture *>(h));
    }

    void EnableScissorRegion(bool enable) override
    {
        SDL_SetRenderClipRect(renderer, enable ? &scissor : nullptr);
        scissor_on = enable;
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        scissor.x = region.Left();
        scissor.y = region.Top();
        scissor.w = region.Width();
        scissor.h = region.Height();
        if (scissor_on) SDL_SetRenderClipRect(renderer, &scissor);
    }

private:
    SDL_Renderer *renderer;
    SDL_BlendMode blend;
    SDL_Rect scissor{};
    bool scissor_on = false;
};

static SDL_Renderer *g_R = nullptr;
static SDL_Window   *g_W = nullptr;
static SystemInterface_SDL g_sys;
static SettingsRenderInterface *g_render = nullptr;
static Rml::Context *g_ctx = nullptr;
static Rml::DataModelHandle model;
static bool g_open = false;
static int  g_capture = -1;          /* the button awaiting its key, or -1 */

/* ---- the data model ----
 *
 * Three booleans riding straight onto options.c (so a toggle takes effect on the next
 * present; the renderer/lighting/DOF all read the options live, issue #69), and the seven
 * key rows as text over the config's real bindings. */
static struct {
    bool engine;
    bool lighting;
    bool dof;
    Rml::String key_name[B_N];
} M;

static int button_from_id(const Rml::String &s)
{
    for (int b = 0; b < B_N; b++)
        if (s == BTN_ID[b]) return b;
    return -1;
}

static void refresh_key_name(int b)
{
    M.key_name[b] = config_key_name(config_key_vk(b));
    model.DirtyVariable(std::string("key_") + BTN_ID[b]);
}

static void model_load(void)
{
    M.engine = opt_renderer_engine() != 0;
    M.lighting = opt_lighting() != 0;
    M.dof = opt_dof() != 0;
    for (int b = 0; b < B_N; b++) refresh_key_name(b);
    model.DirtyAllVariables();
}

/* The booleans ride onto options.c live, every frame the screen is up. The keys are written
 * through config.c at rebind time (and config_save there); this persists the booleans too. */
static void model_store(void)
{
    opt_set_renderer_engine(M.engine);
    opt_set_lighting(M.lighting);
    opt_set_dof(M.dof);
    config_set("renderer", M.engine ? "engine" : "classic");
    config_set("lighting", M.lighting ? "on" : "off");
    config_set("dof", M.dof ? "on" : "off");
    config_save();
}

static void model_store_live(void)
{
    opt_set_renderer_engine(M.engine);
    opt_set_lighting(M.lighting);
    opt_set_dof(M.dof);
}

/* ---- the C API ---- */

int rmlui_init(SDL_Renderer *r, SDL_Window *w)
{
    if (g_ctx) return 1;
    if (!r) return 0;
    g_R = r;
    g_W = w;
    g_sys.SetWindow(w);
    g_render = new SettingsRenderInterface(r);
    Rml::SetSystemInterface(&g_sys);
    Rml::SetRenderInterface(g_render);
    if (!Rml::Initialise()) {
        fprintf(stderr, "rmlui: Rml::Initialise failed\n");
        return 0;
    }
    Rml::LoadFontFace(Rml::Span<const Rml::byte>((const Rml::byte *)lf2_font_sans,
                                                 lf2_font_sans_len),
                      "lf2", Rml::Style::FontStyle::Normal);

    g_ctx = Rml::CreateContext("settings", Rml::Vector2i(640, 480));
    if (!g_ctx) {
        fprintf(stderr, "rmlui: CreateContext failed\n");
        return 0;
    }

    auto ctor = g_ctx->CreateDataModel("settings");
    if (!ctor) {
        fprintf(stderr, "rmlui: CreateDataModel failed\n");
        return 0;
    }
    model = ctor.GetModelHandle();
    ctor.Bind("engine", &M.engine);
    ctor.Bind("lighting", &M.lighting);
    ctor.Bind("dof", &M.dof);
    for (int b = 0; b < B_N; b++)
        ctor.Bind(std::string("key_") + BTN_ID[b], &M.key_name[b]);
    ctor.BindEventCallback("capture", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        const int b = button_from_id(args[0].Get<Rml::String>());
        if (b < 0) return;
        g_capture = b;
        M.key_name[b] = "PRESS KEY";
        model.DirtyVariable(std::string("key_") + BTN_ID[b]);
    });
    ctor.BindEventCallback("close", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        rmlui_close();
    });

    Rml::ElementDocument *doc = g_ctx->LoadDocumentFromMemory(SETTINGS_RML, "settings");
    if (!doc) {
        fprintf(stderr, "rmlui: LoadDocumentFromMemory failed\n");
        return 0;
    }
    doc->Hide();
    model_load();
    return 1;
}

void rmlui_shutdown(void)
{
    if (g_ctx) { Rml::Shutdown(); g_ctx = nullptr; }
    delete g_render;
    g_render = nullptr;
    g_R = nullptr;
    g_W = nullptr;
    g_open = false;
    g_capture = -1;
}

int rmlui_active(void) { return g_open ? 1 : 0; }

void rmlui_open(void)
{
    if (!g_ctx) return;
    g_open = true;
    g_capture = -1;
    model_load();
}

void rmlui_close(void)
{
    if (!g_open) return;
    g_open = false;
    g_capture = -1;
    model_store();
}

void rmlui_render(void)
{
    if (!g_ctx || !g_open || !g_render) return;
    /* The booleans apply live, so a toggle shows on the frame already paused behind it. */
    model_store_live();

    /* The context follows the render output, so the panel stays centred however the window
     * is sized or resized. */
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(g_R, &ow, &oh);
    if (ow > 0 && oh > 0) {
        const Rml::Vector2i d = g_ctx->GetDimensions();
        if (d.x != ow || d.y != oh) g_ctx->SetDimensions(Rml::Vector2i(ow, oh));
    }

    g_render->BeginFrame();
    g_ctx->Update();
    g_ctx->Render();
    g_render->EndFrame();
}

int rmlui_event(SDL_Event *e)
{
    if (!g_ctx || !g_open) return 0;

    /* A rebind capture takes the next key, whichever it is. Escape cancels it. */
    if (e->type == SDL_EVENT_KEY_DOWN && g_capture >= 0) {
        const uint32_t vk = hostwin_key_from_scancode(e->key.scancode);
        if (vk == 0x1B) {
            refresh_key_name(g_capture);
            g_capture = -1;
        } else if (vk != 0) {
            config_set_key_vk(g_capture, vk);
            config_save();
            refresh_key_name(g_capture);
            g_capture = -1;
        }
        return 1;                              /* the binding is ours, not the game's */
    }

    /* Escape closes the screen (and is consumed, so it does not also unpause). */
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_ESCAPE) {
        rmlui_close();
        return 1;
    }

    /* Everything else goes to the document. SDL delivers the pointer in POINTS; the context
     * is sized in pixels, so at a density above 1 the coordinates are scaled first. */
    SDL_Event copy = *e;
    const float density = SDL_GetWindowPixelDensity(g_W);
    if (density > 1.0f) {
        switch (e->type) {
        case SDL_EVENT_MOUSE_MOTION:   copy.motion.x *= density; copy.motion.y *= density; break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: copy.button.x *= density; copy.button.y *= density; break;
        default: break;
        }
    }
    const bool propagating = RmlSDL::InputEventHandler(g_ctx, g_W, copy);
    return propagating ? 0 : 1;
}
