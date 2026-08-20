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
#include "rmlui_backend.h"

extern "C" {
#include "config.h"
#include "bindings.h"
#include "hostwin.h"
#include "options.h"
}

#include <cstdio>
#include <cstdlib>
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
 * glue constructs below; the checkboxes write the engine/lighting booleans back to
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
    display: inline-block; width: 105px; text-align: center;
    background: #102040; color: #e0e8f0; padding: 2px 0;
  }
  .row .key:focus { background: #4870a0; color: #fff; }
  .heads { margin-top: 6px; color: #8090a8; }
  .heads .label { display: inline-block; width: 190px; }
  .heads .device { display: inline-block; width: 109px; text-align: center; }
  .device-icon { width: 30px; height: 30px; }
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
    <div class="row"><span class="label">Render engine</span><input id="engine" type="checkbox" data-bind="checked: engine"/></div>
    <div class="row"><span class="label">Lighting</span><input type="checkbox" data-bind="checked: lighting"/></div>
    <div class="group">CONTROLS</div>
    <div class="heads"><span class="label"></span><span class="device"><img class="device-icon" src="device_keyboard.svg"/></span><span class="device"><img class="device-icon" src="device_gamepad.svg"/></span></div>
    <div class="row"><span class="label">Up</span><button class="key" data-event-click="capture_key('up')"><span data-bind="text: key_up"/></button><button class="key" data-event-click="capture_pad('up')"><span data-bind="text: pad_up"/></button></div>
    <div class="row"><span class="label">Down</span><button class="key" data-event-click="capture_key('down')"><span data-bind="text: key_down"/></button><button class="key" data-event-click="capture_pad('down')"><span data-bind="text: pad_down"/></button></div>
    <div class="row"><span class="label">Left</span><button class="key" data-event-click="capture_key('left')"><span data-bind="text: key_left"/></button><button class="key" data-event-click="capture_pad('left')"><span data-bind="text: pad_left"/></button></div>
    <div class="row"><span class="label">Right</span><button class="key" data-event-click="capture_key('right')"><span data-bind="text: key_right"/></button><button class="key" data-event-click="capture_pad('right')"><span data-bind="text: pad_right"/></button></div>
    <div class="row"><span class="label">Attack</span><button class="key" data-event-click="capture_key('attack')"><span data-bind="text: key_attack"/></button><button class="key" data-event-click="capture_pad('attack')"><span data-bind="text: pad_attack"/></button></div>
    <div class="row"><span class="label">Jump</span><button class="key" data-event-click="capture_key('jump')"><span data-bind="text: key_jump"/></button><button class="key" data-event-click="capture_pad('jump')"><span data-bind="text: pad_jump"/></button></div>
    <div class="row"><span class="label">Defend</span><button class="key" data-event-click="capture_key('defend')"><span data-bind="text: key_defend"/></button><button class="key" data-event-click="capture_pad('defend')"><span data-bind="text: pad_defend"/></button></div>
    <button id="close" data-event-click="close">CLOSE</button>
  </div>
</body>
</rml>
)RML";

static SDL_Renderer *g_R = nullptr;
static SDL_Window   *g_W = nullptr;
static SystemInterface_SDL g_sys;
static RmlUiRenderBackend *g_render = nullptr;
static Rml::Context *g_ctx = nullptr;
static Rml::ElementDocument *g_doc = nullptr;
static Rml::DataModelHandle model;
static bool g_open = false;
static int g_key_capture = -1;
static int g_pad_capture = -1;
static bool g_pad_capture_armed;
static bool g_dispatching_pad;
static long g_open_count;
static long g_render_frames;

/* ---- the data model ----
 *
 * Three booleans riding straight onto options.c (so a toggle takes effect on the next
 * present; the renderer and character lighting read the options live, issue #69), and the seven
 * key rows as text over the config's real bindings. */
static struct {
    bool engine;
    bool lighting;
    Rml::String key_name[B_N];
    Rml::String pad_name[B_N];
} M;

static int button_from_id(const Rml::String &s)
{
    for (int b = 0; b < B_N; b++)
        if (s == binding_action_id(b)) return b;
    return -1;
}

static void refresh_key_name(int b)
{
    M.key_name[b] = binding_key_name(binding_key_vk(b));
    model.DirtyVariable(std::string("key_") + binding_action_id(b));
}

static void refresh_pad_name(int b)
{
    M.pad_name[b] = binding_pad_name(binding_pad_button(b));
    model.DirtyVariable(std::string("pad_") + binding_action_id(b));
}

static void model_load(void)
{
    M.engine = opt_renderer_engine() != 0;
    M.lighting = opt_lighting() != 0;
    for (int b = 0; b < B_N; b++) { refresh_key_name(b); refresh_pad_name(b); }
    model.DirtyAllVariables();
}

/* The booleans ride onto options.c live, every frame the screen is up. The keys are written
 * through config.c at rebind time (and config_save there); this persists the booleans too. */
static void model_store(void)
{
    opt_set_renderer_engine(M.engine);
    opt_set_lighting(M.lighting);
    config_set("renderer", M.engine ? "engine" : "classic");
    config_set("lighting", M.lighting ? "on" : "off");
    config_save();
}

static void model_store_live(void)
{
    opt_set_renderer_engine(M.engine);
    opt_set_lighting(M.lighting);
}

/* ---- the C API ---- */

int rmlui_init(SDL_Renderer *r, SDL_Window *w)
{
    if (g_ctx) return 1;
    if (!r) return 0;
    g_R = r;
    g_W = w;
    g_sys.SetWindow(w);
    g_render = new RmlUiRenderBackend(r);
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
    for (int b = 0; b < B_N; b++) {
        ctor.Bind(std::string("key_") + binding_action_id(b), &M.key_name[b]);
        ctor.Bind(std::string("pad_") + binding_action_id(b), &M.pad_name[b]);
    }
    ctor.BindEventCallback("capture_key", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        const int b = button_from_id(args[0].Get<Rml::String>());
        if (b < 0) return;
        g_key_capture = b;
        g_pad_capture = -1;
        M.key_name[b] = "PRESS KEY";
        model.DirtyVariable(std::string("key_") + binding_action_id(b));
    });
    ctor.BindEventCallback("capture_pad", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        const int b = button_from_id(args[0].Get<Rml::String>());
        if (b < 0) return;
        g_pad_capture = b;
        g_key_capture = -1;
        g_pad_capture_armed = !g_dispatching_pad;
        M.pad_name[b] = g_pad_capture_armed ? "PRESS BUTTON" : "RELEASE BUTTON";
        model.DirtyVariable(std::string("pad_") + binding_action_id(b));
    });
    ctor.BindEventCallback("close", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        rmlui_close();
    });

    g_doc = g_ctx->LoadDocumentFromMemory(SETTINGS_RML, "settings");
    if (!g_doc) {
        fprintf(stderr, "rmlui: LoadDocumentFromMemory failed\n");
        return 0;
    }
    g_doc->Hide();
    model_load();
    return 1;
}

void rmlui_shutdown(void)
{
    const int shared_textures = g_render ? g_render->SharedDeviceTexturesLoaded() : 0;
    if (getenv("LF2_RMLUI_DEBUG"))
        fprintf(stderr, "rmlui: %ld settings open(s), %ld rendered frame(s), %d shared SVG "
                        "device texture(s) loaded\n",
                g_open_count, g_render_frames, shared_textures);
    if (g_ctx) { Rml::Shutdown(); g_ctx = nullptr; }
    g_doc = nullptr;
    delete g_render;
    g_render = nullptr;
    g_R = nullptr;
    g_W = nullptr;
    g_open = false;
    g_key_capture = g_pad_capture = -1;
    g_open_count = g_render_frames = 0;
}

int rmlui_active(void) { return g_open ? 1 : 0; }

void rmlui_open(void)
{
    if (!g_ctx) return;
    g_open = true;
    g_open_count++;
    g_key_capture = g_pad_capture = -1;
    model_load();
    if (g_doc) {
        g_doc->Show();
        if (Rml::Element *first = g_doc->GetElementById("engine")) first->Focus();
    }
}

void rmlui_close(void)
{
    if (!g_open) return;
    g_open = false;
    g_key_capture = g_pad_capture = -1;
    model_store();
    if (g_doc) g_doc->Hide();
}

void rmlui_render(void)
{
    if (!g_ctx || !g_open || !g_render) return;
    g_render_frames++;
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
    if (e->type == SDL_EVENT_KEY_DOWN && g_key_capture >= 0) {
        const uint32_t vk = hostwin_key_from_scancode(e->key.scancode);
        if (vk == 0x1B) {
            refresh_key_name(g_key_capture);
            g_key_capture = -1;
        } else if (vk != 0) {
            binding_set_key_vk(g_key_capture, vk);
            config_save();
            refresh_key_name(g_key_capture);
            g_key_capture = -1;
        }
        return 1;                              /* the binding is ours, not the game's */
    }

    if (g_pad_capture >= 0 && (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN
                           || e->type == SDL_EVENT_GAMEPAD_BUTTON_UP)) {
        if (e->type == SDL_EVENT_GAMEPAD_BUTTON_UP && !g_pad_capture_armed) {
            g_pad_capture_armed = true;
            M.pad_name[g_pad_capture] = "PRESS BUTTON";
            model.DirtyVariable(std::string("pad_") + binding_action_id(g_pad_capture));
        } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && g_pad_capture_armed) {
            binding_set_pad_button(g_pad_capture, (SDL_GamepadButton)e->gbutton.button);
            config_save();
            refresh_pad_name(g_pad_capture);
            g_pad_capture = -1;
        }
        return 1;
    }

    /* Escape closes the screen (and is consumed, so it does not also unpause). */
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_ESCAPE) {
        rmlui_close();
        return 1;
    }

    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const SDL_GamepadButton b = (SDL_GamepadButton)e->gbutton.button;
        if (b == SDL_GAMEPAD_BUTTON_EAST) { rmlui_close(); return 1; }
        Rml::Input::KeyIdentifier key = Rml::Input::KI_UNKNOWN;
        int mods = 0;
        if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN || b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
            key = Rml::Input::KI_TAB;
        else if (b == SDL_GAMEPAD_BUTTON_DPAD_UP || b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
            key = Rml::Input::KI_TAB; mods = Rml::Input::KM_SHIFT;
        } else if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START)
            key = Rml::Input::KI_RETURN;
        if (key != Rml::Input::KI_UNKNOWN) {
            g_dispatching_pad = true;
            g_ctx->ProcessKeyDown(key, mods);
            g_dispatching_pad = false;
            return 1;
        }
    }
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_UP) return 1;

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
