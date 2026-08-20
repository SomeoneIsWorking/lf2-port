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
#include "hd2d.h"
#include "hostwin.h"
#include "keyboard.h"
#include "options.h"
}

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

/* The port's committed font face, compiled in by CMakeLists.txt. */
extern const unsigned char lf2_font_sans[];
extern const unsigned int lf2_font_sans_len;

/* ---- the document, embedded ----
 *
 * The settings screen is small and its data few, so the whole thing -- markup, style, the
 * data-model hookup -- is one string. `data-model="settings"` binds the body to the model the
 * glue constructs below; the checkboxes write the engine/lighting booleans back to
 * options.c every frame the screen is up, and each key row shows the config's current binding
 * and starts a rebind when clicked (its next key press is the binding).
 */
/* Dusklight's window.rcss and SettingsWindow structure, adapted to LF2's smaller set of
 * settings. The important parts are copied rather than approximated: a flex body centres one
 * modal window, the window owns a tab strip and scrollable content pane, and every interactive
 * element is a focusable RmlUi control. */
static const char SETTINGS_RML[] = R"RML(
<rml>
<head>
<style>
  *, *:before, *:after { box-sizing: border-box; }
  body {
    display: flex;
    width: 100%;
    height: 100%;
    padding: 32dp;
    font-family: lf2;
    font-size: 16dp;
    color: #e0dbc8;
  }
  window {
    display: flex;
    flex-flow: column;
    position: relative;
    width: 100%;
    height: 100%;
    max-width: 900dp;
    max-height: 680dp;
    margin: auto;
    overflow: hidden;
    border: 2dp #92875b;
    border-radius: 14dp;
    background-color: rgba(21, 22, 16, 94%);
  }
  tab-bar {
    display: flex;
    flex: 0 0 58dp;
    height: 58dp;
    padding: 8dp;
    gap: 8dp;
    border-bottom: 2dp #92875b;
    background-color: rgba(217, 217, 217, 10%);
  }
  tab-bar button { flex: 1 1 0; }
  content {
    display: flex;
    flex: 1 1 auto;
    min-width: 0;
    min-height: 0;
    overflow: hidden;
  }
  pane {
    display: flex;
    flex-flow: column;
    flex: 1 1 auto;
    min-width: 0;
    min-height: 0;
    padding: 20dp 24dp;
    gap: 8dp;
    overflow: hidden auto;
  }
  pane > * { flex: 0 0 auto; }
  .section-heading {
    display: block;
    padding-top: 8dp;
    font-size: 20dp;
    color: rgba(224, 219, 200, 55%);
  }
  button, select-button {
    display: block;
    text-align: center;
    padding: 8dp 16dp;
    border-radius: 14dp;
    border: 1dp rgba(146, 135, 91, 55%);
    background: rgba(17, 16, 10, 35%);
    color: #e0dbc8;
    focus: auto;
    tab-index: auto;
  }
  button:hover, button:focus, button:focus-visible,
  select-button:hover, select-button:focus, select-button:focus-visible {
    background: rgba(204, 184, 119, 25%);
    border: 2dp #c2a42d;
  }
  .danger { color: #ffb0a0; }
  .setting-row, .binding-row, .device-heads {
    display: flex;
    align-items: center;
    gap: 8dp;
    min-height: 38dp;
  }
  .label { display: block; flex: 1 1 auto; min-width: 150dp; }
  .setting-value { display: block; flex: 0 0 240dp; }
  input[type=checkbox] { width: 24dp; height: 24dp; focus: auto; }
  input[type=range] { width: 220dp; height: 28dp; focus: auto; }
  input[type=range] slidertrack { height: 8dp; background: #403d31; }
  input[type=range] sliderbar { width: 18dp; height: 24dp; margin-top: -8dp; background: #c2a42d; }
  input[type=range] sliderarrowdec, input[type=range] sliderarrowinc { width: 0; height: 0; }
  .range-value { display: block; width: 58dp; text-align: right; }
  .key { flex: 0 0 128dp; }
  .device-heads .device { display: block; flex: 0 0 128dp; text-align: center; }
  .device-icon { width: 30dp; height: 30dp; }
  scrollbarvertical { width: 8dp; margin-left: 4dp; }
  scrollbarvertical sliderbar { width: 8dp; min-height: 24dp; background: rgba(224,219,200,45%); }
  scrollbarhorizontal { height: 0; }
  @media (max-height: 560dp) {
    body { padding: 12dp; }
    window { border-radius: 8dp; }
  }
</style>
</head>
<body data-model="settings">
  <window id="window">
    <tab-bar>
      <button id="game-tab" data-event-click="show_page('game')">GAME</button>
      <button data-event-click="show_page('graphics')">GRAPHICS</button>
      <button data-event-click="show_page('controls')">CONTROLS</button>
    </tab-bar>
    <content>
      <pane data-if="page == 'game'">
        <span class="section-heading">PORT MENU</span>
        <button id="continue" data-event-click="close">CONTINUE</button>
        <button data-if="can_drop" data-event-click="drop_out">DROP OUT</button>
        <button data-if="in_match" data-event-click="leave_match">LEAVE MATCH</button>
        <button class="danger" data-event-click="quit">QUIT GAME</button>
      </pane>
      <pane data-if="page == 'graphics'">
        <span class="section-heading">RENDERING</span>
        <div class="setting-row"><span class="label">Native renderer</span><button class="setting-value" data-event-click="toggle_engine">{{engine ? 'ON' : 'OFF'}}</button></div>
        <div class="setting-row"><span class="label">Character shading and shadows</span><button class="setting-value" data-event-click="toggle_lighting">{{lighting ? 'ON' : 'OFF'}}</button></div>
        <span class="section-heading">LIGHT DIRECTION</span>
        <div class="setting-row"><span class="label">Angle</span><input class="setting-value" type="range" min="-180" max="180" step="5" data-value="light_angle"/><span class="range-value">{{light_angle}}°</span></div>
        <div class="setting-row"><span class="label">Height</span><input class="setting-value" type="range" min="5" max="85" step="5" data-value="light_height"/><span class="range-value">{{light_height}}°</span></div>
      </pane>
      <pane data-if="page == 'controls'">
        <span class="section-heading">INPUT MAPPING</span>
        <div class="device-heads"><span class="label"></span><span class="device"><img class="device-icon" src="device_keyboard.svg"/></span><span class="device"><img class="device-icon" src="device_gamepad.svg"/></span></div>
        <div class="binding-row"><span class="label">Up</span><button class="key" data-event-click="capture_key('up')">{{key_up}}</button><button class="key" data-event-click="capture_pad('up')">{{pad_up}}</button></div>
        <div class="binding-row"><span class="label">Down</span><button class="key" data-event-click="capture_key('down')">{{key_down}}</button><button class="key" data-event-click="capture_pad('down')">{{pad_down}}</button></div>
        <div class="binding-row"><span class="label">Left</span><button class="key" data-event-click="capture_key('left')">{{key_left}}</button><button class="key" data-event-click="capture_pad('left')">{{pad_left}}</button></div>
        <div class="binding-row"><span class="label">Right</span><button class="key" data-event-click="capture_key('right')">{{key_right}}</button><button class="key" data-event-click="capture_pad('right')">{{pad_right}}</button></div>
        <div class="binding-row"><span class="label">Attack</span><button class="key" data-event-click="capture_key('attack')">{{key_attack}}</button><button class="key" data-event-click="capture_pad('attack')">{{pad_attack}}</button></div>
        <div class="binding-row"><span class="label">Jump</span><button class="key" data-event-click="capture_key('jump')">{{key_jump}}</button><button class="key" data-event-click="capture_pad('jump')">{{pad_jump}}</button></div>
        <div class="binding-row"><span class="label">Defend</span><button class="key" data-event-click="capture_key('defend')">{{key_defend}}</button><button class="key" data-event-click="capture_pad('defend')">{{pad_defend}}</button></div>
      </pane>
    </content>
  </window>
</body>
</rml>
)RML";

static SDL_Renderer *g_R = nullptr;
static SDL_Window *g_W = nullptr;
static RmlUiRenderBackend *g_render = nullptr;
static Rml::Context *g_ctx = nullptr;
static Rml::ElementDocument *g_doc = nullptr;
static bool g_open = false;
static int g_key_capture = -1;
static int g_pad_capture = -1;
static bool g_pad_capture_armed;
static bool g_dispatching_pad;
static long g_open_count;
static long g_render_frames;
static unsigned char g_nav_previous[7];

static SystemInterface_SDL &system_interface()
{
    static SystemInterface_SDL value;
    return value;
}

static Rml::DataModelHandle &data_model()
{
    static Rml::DataModelHandle value;
    return value;
}

/* ---- the data model ----
 *
 * Three booleans riding straight onto options.c (so a toggle takes effect on the next
 * present; the renderer and character lighting read the options live, issue #69), and the seven
 * key rows as text over the config's real bindings. */
static struct {
    bool engine;
    bool lighting;
    bool in_match;
    bool can_drop;
    int light_angle;
    int light_height;
    Rml::String page;
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
    data_model().DirtyVariable(std::string("key_") + binding_action_id(b));
}

static void refresh_pad_name(int b)
{
    M.pad_name[b] = binding_pad_name(binding_pad_button(b));
    data_model().DirtyVariable(std::string("pad_") + binding_action_id(b));
}

static void model_load(void)
{
    float angle = 0.0f, height = 0.0f;
    M.engine = opt_renderer_engine() != 0;
    M.lighting = opt_lighting() != 0;
    M.in_match = pause_menu_in_match() != 0;
    M.can_drop = pause_menu_can_drop() != 0;
    hd2d_light_angles(&angle, &height);
    M.light_angle = (int)(angle + (angle < 0.0f ? -0.5f : 0.5f));
    M.light_height = (int)std::lround(height);
    M.page = "game";
    for (int b = 0; b < B_N; b++) {
        refresh_key_name(b);
        refresh_pad_name(b);
    }
    data_model().DirtyAllVariables();
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
    hd2d_light_set_angles((float)M.light_angle, (float)M.light_height);
}

/* Dusklight updates controller navigation from the live pad state, including repeat handling,
 * instead of assuming every platform emits an SDL_GAMEPAD_BUTTON event. LF2's virtual-pad
 * tests exposed why that matters: its d-pad is visible through polling on every frame but some
 * SDL backends only emit joystick-class events. Edge-driven polling makes the shipping mapping
 * and the test device follow the same route. */
static void poll_gamepad_navigation(void)
{
    unsigned char now[7] = {};
    for (int pad = 0; pad < 2; pad++) {
        unsigned char state[7] = {};
        if (!gamepad_player_buttons(pad, state)) continue;
        for (int i = 0; i < 7; i++) now[i] |= state[i];
    }
    const auto pressed = [&now](int i) { return now[i] && !g_nav_previous[i]; };
    if (getenv("LF2_RMLUI_DEBUG") && (pressed(B_UP) || pressed(B_DOWN) || pressed(B_LEFT) || pressed(B_RIGHT) ||
                                      pressed(B_ATTACK) || pressed(B_JUMP))) {
        Rml::Element *focus = g_ctx->GetFocusElement();
        fprintf(stderr, "rmlui nav: %d%d%d%d%d%d%d focus=%s#%s\n", now[0], now[1], now[2], now[3], now[4], now[5],
                now[6], focus ? focus->GetTagName().c_str() : "none", focus ? focus->GetId().c_str() : "");
    }
    const auto move_focus = [](bool forward) {
        Rml::Element *from = g_ctx->GetFocusElement();
        if (!from || !g_doc) return;
        if (Rml::Element *next = g_doc->FindNextTabElement(from, forward)) {
            next->Focus(true);
            next->ScrollIntoView(Rml::ScrollAlignment::Nearest);
        }
    };
    if (pressed(B_UP)) move_focus(false);
    if (pressed(B_DOWN)) move_focus(true);
    if (pressed(B_LEFT)) g_ctx->ProcessKeyDown(Rml::Input::KI_LEFT, 0);
    if (pressed(B_RIGHT)) g_ctx->ProcessKeyDown(Rml::Input::KI_RIGHT, 0);
    if (pressed(B_ATTACK) || pressed(B_JUMP)) {
        g_dispatching_pad = true;
        if (Rml::Element *focused = g_ctx->GetFocusElement()) focused->Click();
        g_dispatching_pad = false;
    }
    std::memcpy(g_nav_previous, now, sizeof g_nav_previous);
}

/* ---- the C API ---- */

int rmlui_init(SDL_Renderer *r, SDL_Window *w)
{
    if (g_ctx) return 1;
    if (!r) return 0;
    g_R = r;
    g_W = w;
    system_interface().SetWindow(w);
    g_render = new RmlUiRenderBackend(r);
    Rml::SetSystemInterface(&system_interface());
    Rml::SetRenderInterface(g_render);
    if (!Rml::Initialise()) {
        fprintf(stderr, "rmlui: Rml::Initialise failed\n");
        return 0;
    }
    Rml::LoadFontFace(Rml::Span<const Rml::byte>((const Rml::byte *)lf2_font_sans, lf2_font_sans_len), "lf2",
                      Rml::Style::FontStyle::Normal);

    g_ctx = Rml::CreateContext("settings", Rml::Vector2i(640, 480));
    if (!g_ctx) {
        fprintf(stderr, "rmlui: CreateContext failed\n");
        return 0;
    }
    g_ctx->SetDensityIndependentPixelRatio(SDL_GetWindowPixelDensity(w));

    auto ctor = g_ctx->CreateDataModel("settings");
    if (!ctor) {
        fprintf(stderr, "rmlui: CreateDataModel failed\n");
        return 0;
    }
    data_model() = ctor.GetModelHandle();
    ctor.Bind("engine", &M.engine);
    ctor.Bind("lighting", &M.lighting);
    ctor.Bind("in_match", &M.in_match);
    ctor.Bind("can_drop", &M.can_drop);
    ctor.Bind("light_angle", &M.light_angle);
    ctor.Bind("light_height", &M.light_height);
    ctor.Bind("page", &M.page);
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
        data_model().DirtyVariable(std::string("key_") + binding_action_id(b));
    });
    ctor.BindEventCallback("capture_pad", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        const int b = button_from_id(args[0].Get<Rml::String>());
        if (b < 0) return;
        g_pad_capture = b;
        g_key_capture = -1;
        g_pad_capture_armed = !g_dispatching_pad;
        M.pad_name[b] = g_pad_capture_armed ? "PRESS BUTTON" : "RELEASE BUTTON";
        data_model().DirtyVariable(std::string("pad_") + binding_action_id(b));
    });
    ctor.BindEventCallback("show_page", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        M.page = args[0].Get<Rml::String>();
        data_model().DirtyVariable("page");
    });
    ctor.BindEventCallback("toggle_engine", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        M.engine = !M.engine;
        data_model().DirtyVariable("engine");
    });
    ctor.BindEventCallback("toggle_lighting", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        M.lighting = !M.lighting;
        data_model().DirtyVariable("lighting");
    });
    ctor.BindEventCallback("close",
                           [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { pause_menu_close(); });
    ctor.BindEventCallback("drop_out",
                           [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { pause_menu_drop_out(); });
    ctor.BindEventCallback(
        "leave_match", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { pause_menu_leave_match(); });
    ctor.BindEventCallback(
        "quit", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { hostwin_request_quit(); });

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
        fprintf(stderr,
                "rmlui: %ld settings open(s), %ld rendered frame(s), %d shared SVG "
                "device texture(s) loaded\n",
                g_open_count, g_render_frames, shared_textures);
    if (g_ctx) {
        Rml::Shutdown();
        g_ctx = nullptr;
    }
    g_doc = nullptr;
    delete g_render;
    g_render = nullptr;
    g_R = nullptr;
    g_W = nullptr;
    g_open = false;
    g_key_capture = g_pad_capture = -1;
    std::memset(g_nav_previous, 0, sizeof g_nav_previous);
    g_open_count = g_render_frames = 0;
}

int rmlui_active(void) { return g_open ? 1 : 0; }

void rmlui_open(void)
{
    if (!g_ctx) return;
    g_open = true;
    g_open_count++;
    g_key_capture = g_pad_capture = -1;
    std::memset(g_nav_previous, 0, sizeof g_nav_previous);
    model_load();
    if (g_doc) {
        g_doc->Show();
        if (Rml::Element *first = g_doc->GetElementById("continue")) first->Focus();
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
    const float density = SDL_GetWindowPixelDensity(g_W);
    if (density > 0.0f && density != g_ctx->GetDensityIndependentPixelRatio())
        g_ctx->SetDensityIndependentPixelRatio(density);

    poll_gamepad_navigation();
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
        const uint32_t vk = keyboard_vk_from_scancode(e->key.scancode);
        if (vk == 0x1B) {
            refresh_key_name(g_key_capture);
            g_key_capture = -1;
        } else if (vk != 0) {
            binding_set_key_vk(g_key_capture, vk);
            config_save();
            refresh_key_name(g_key_capture);
            g_key_capture = -1;
        }
        return 1; /* the binding is ours, not the game's */
    }

    /* Dusklight's Button component turns Confirm into a click on its focused generic element.
     * LF2 keeps its document data-bound, so reproduce that document-level behavior here. */
    if (e->type == SDL_EVENT_KEY_DOWN &&
        (e->key.scancode == SDL_SCANCODE_RETURN || e->key.scancode == SDL_SCANCODE_SPACE)) {
        if (Rml::Element *focused = g_ctx->GetFocusElement()) focused->Click();
        return 1;
    }

    if (g_pad_capture >= 0 && (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type == SDL_EVENT_GAMEPAD_BUTTON_UP)) {
        if (e->type == SDL_EVENT_GAMEPAD_BUTTON_UP && !g_pad_capture_armed) {
            g_pad_capture_armed = true;
            M.pad_name[g_pad_capture] = "PRESS BUTTON";
            data_model().DirtyVariable(std::string("pad_") + binding_action_id(g_pad_capture));
        } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && g_pad_capture_armed) {
            binding_set_pad_button(g_pad_capture, (SDL_GamepadButton)e->gbutton.button);
            config_save();
            refresh_pad_name(g_pad_capture);
            g_pad_capture = -1;
        }
        return 1;
    }

    /* Escape's physical state is the one toggle owned by pause_tick. Consume the SDL event,
     * but do not close here: closing during the pump and then edge-polling the same held key
     * would immediately reopen the document in the game update that follows. */
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_ESCAPE) { return 1; }

    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const SDL_GamepadButton b = (SDL_GamepadButton)e->gbutton.button;
        if (b == SDL_GAMEPAD_BUTTON_EAST) {
            pause_menu_close();
            return 1;
        }
        return 1; /* navigation itself is edge-polled once per rendered frame */
    }
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_UP) return 1;

    /* Everything else goes to the document. SDL delivers the pointer in POINTS; the context
     * is sized in pixels, so at a density above 1 the coordinates are scaled first. */
    SDL_Event copy = *e;
    const float density = SDL_GetWindowPixelDensity(g_W);
    if (density > 1.0f) {
        switch (e->type) {
        case SDL_EVENT_MOUSE_MOTION:
            copy.motion.x *= density;
            copy.motion.y *= density;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            copy.button.x *= density;
            copy.button.y *= density;
            break;
        default: break;
        }
    }
    RmlSDL::InputEventHandler(g_ctx, g_W, copy);
    /* Dusklight blocks the game whenever any active document is visible. The SDL adapter's
     * propagation result only describes RmlUi's DOM, not whether the guest should also see
     * the physical input, so every input event is consumed at this boundary. */
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: return 1;
    default: return 0;
    }
}
