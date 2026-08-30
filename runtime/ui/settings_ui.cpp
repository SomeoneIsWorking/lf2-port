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
#include "rmlui_input.h"
#include "rmlui_lifecycle.h"
#include "rmlui_system.h"

extern "C" {
#include "config.h"
#include "bindings.h"
#include "cheats.h"
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
  .pass-kind, .pass-factor, .pass-drop { flex: 0 0 96dp; margin-left: 4dp; }
  .cheat { display: flex; flex-flow: column; text-align: left; gap: 3dp; }
  .cheat-title { display: block; color: #e0dbc8; }
  .cheat-detail { display: block; color: rgba(224, 219, 200, 60%); font-size: 13dp; }
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
      <button data-event-click="show_page('cheats')">CHEATS</button>
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
        <div class="setting-row"><span class="label">Intensity</span><input class="setting-value" type="range" min="25" max="300" step="5" data-value="light_pct"/><span class="range-value">{{light_pct}}%</span></div>
        <span class="section-heading">SPRITE PASSES</span>
        <!-- PASS_ROWS -->
        <div class="setting-row" data-if="pass_count < pass_max"><span class="label">Add a pass</span><button class="setting-value" data-event-click="add_pass">ADD</button></div>
        <div class="setting-row"><span class="label">Edge smoothing</span><button class="setting-value" data-event-click="toggle_aa">{{aa ? 'ON' : 'OFF'}}</button></div>
        <div class="setting-row"><span class="label">Inner contour</span><button class="setting-value" data-event-click="toggle_inner">{{inner ? 'ON' : 'OFF'}}</button></div>
        <div class="setting-row"><span class="label">Outline</span><button class="setting-value" data-event-click="cycle_outline">{{outline_name}}</button></div>
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
        <span class="section-heading">TOUCH</span>
        <div class="setting-row"><span class="label">Touch controls</span><button class="setting-value" data-event-click="toggle_touch_controls">{{touch_controls ? 'ON' : 'OFF'}}</button></div>
      </pane>
      <pane data-if="page == 'cheats'">
        <span class="section-heading">CHEATS</span>
        <!-- CHEAT_BUTTONS -->
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
static RmlUiLifecycle g_lifecycle;
static int g_key_capture = -1;
static int g_pad_capture = -1;
static bool g_pad_capture_armed;
static bool g_dispatching_pad;
static long g_open_count;
static long g_render_frames;
static int g_metrics_reported;

static std::string settings_document()
{
    std::string document = SETTINGS_RML;
    const std::string marker = "<!-- CHEAT_BUTTONS -->";
    std::string buttons;
    size_t count = 0;
    const CheatDescriptor *descriptors = cheats_descriptors(&count);
    for (size_t i = 0; i < count; ++i) {
        const CheatDescriptor &entry = descriptors[i];
        buttons += "<button class=\"cheat\"";
        if (entry.match_only) buttons += " data-if=\"in_match\"";
        buttons += " data-event-click=\"activate_cheat('";
        buttons += entry.id;
        buttons += "')\"><span class=\"cheat-title\">";
        buttons += entry.key;
        buttons += " · ";
        buttons += entry.label;
        buttons += "</span><span class=\"cheat-detail\">";
        buttons += entry.detail;
        buttons += "</span></button>";
    }
    document.replace(document.find(marker), marker.size(), buttons);

    /* THE PASS ROWS ARE GENERATED, one per pass the chain may hold, because how many that is
     * belongs to spritefilter.h -- writing them out in the markup would put the cap in two
     * places and let the menu drift from what parses. */
    const std::string pass_marker = "<!-- PASS_ROWS -->";
    std::string rows;
    for (int i = 0; i < SPRITE_PASS_MAX; ++i) {
        const std::string n = std::to_string(i);
        rows += "<div class=\"setting-row\" data-if=\"pass_count > " + n + "\"><span class=\"label\">Pass " +
                std::to_string(i + 1) + "</span><button class=\"pass-kind\" data-event-click=\"cycle_pass_kind('" + n +
                "')\">{{pass_kind" + n + "}}</button><button class=\"pass-factor\" data-event-click=\"" +
                "cycle_pass_factor('" + n + "')\">{{pass_factor" + n + "}}</button><button class=\"pass-drop\" " +
                "data-event-click=\"remove_pass('" + n + "')\">REMOVE</button></div>";
    }
    document.replace(document.find(pass_marker), pass_marker.size(), rows);
    return document;
}

static Rml::DataModelHandle &data_model()
{
    static Rml::DataModelHandle value;
    return value;
}

static float content_scale()
{
    const float scale = SDL_GetWindowDisplayScale(g_W);
    if (scale > 0.0f) return scale;
    static bool reported;
    if (!reported) {
        reported = true;
        fprintf(stderr, "rmlui: SDL_GetWindowDisplayScale failed: %s; using 1.0 for UI layout\n", SDL_GetError());
    }
    return 1.0f;
}

/* ---- the data model ----
 *
 * Three booleans riding straight onto options.c (the renderer and character lighting read the
 * options on the next live game frame, issue #69), and the seven key rows as text over the
 * config's real bindings. */
static struct {
    bool engine;
    bool lighting;
    bool touch_controls;
    bool in_match;
    bool can_drop;
    int light_angle;
    int light_height;
    int light_pct; /* key-light strength as a percentage of a flat key (#111) */
    /* THE SAMPLING CHAIN BEING EDITED (#112). The menu owns a whole SpriteChain and hands it
     * to options.c on every live update, so the rules about what a chain may hold stay in
     * spritefilter.h -- the rows below only ask it to step a value and re-read the labels. */
    SpriteChain chain;
    int pass_count;
    int pass_max;
    bool aa;
    bool inner;
    Rml::String page;
    Rml::String key_name[B_N];
    Rml::String pad_name[B_N];
    Rml::String pass_kind[SPRITE_PASS_MAX];
    Rml::String pass_factor[SPRITE_PASS_MAX];
    Rml::String outline_name;
} M;

static void refresh_sprite_rows(void)
{
    M.pass_count = M.chain.count;
    M.pass_max = SPRITE_PASS_MAX;
    M.aa = M.chain.smooth != 0;
    M.inner = M.chain.inner != 0;
    for (int i = 0; i < SPRITE_PASS_MAX; i++) {
        char buf[16] = "";
        if (i < M.chain.count) spritechain_factor_label(&M.chain.pass[i], buf, sizeof buf);
        M.pass_kind[i] = i < M.chain.count ? spritechain_kind_label(&M.chain.pass[i]) : "";
        M.pass_factor[i] = buf;
        data_model().DirtyVariable("pass_kind" + std::to_string(i));
        data_model().DirtyVariable("pass_factor" + std::to_string(i));
    }
    M.outline_name = M.chain.outline ? std::to_string(M.chain.outline) + " PX" : "OFF";
    data_model().DirtyVariable("pass_count");
    data_model().DirtyVariable("aa");
    data_model().DirtyVariable("inner");
    data_model().DirtyVariable("outline_name");
}

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
    M.touch_controls = opt_touch_controls() != 0;
    M.in_match = pause_menu_in_match() != 0;
    M.can_drop = pause_menu_can_drop() != 0;
    hd2d_light_angles(&angle, &height);
    M.light_angle = (int)(angle + (angle < 0.0f ? -0.5f : 0.5f));
    M.light_height = (int)std::lround(height);
    float intensity = 1.48f;
    hd2d_light_intensity(&intensity);
    M.light_pct = (int)std::lround(intensity * 100.0f);
    M.chain = *opt_sprite_chain();
    refresh_sprite_rows();
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
    opt_set_touch_controls(M.touch_controls);
    opt_set_sprite_chain(&M.chain);
    char passes[128];
    spritechain_format(&M.chain, passes, sizeof passes);
    config_set("sprite_passes", passes);
    char buf[32];
    config_set("renderer", M.engine ? "engine" : "classic");
    config_set("lighting", M.lighting ? "on" : "off");
    config_set("touch_controls", M.touch_controls ? "on" : "off");
    snprintf(buf, sizeof buf, "%.2f", (double)((float)M.light_pct / 100.0f));
    config_set("light_intensity", buf);
    config_save();
}

static void model_store_live(void)
{
    opt_set_renderer_engine(M.engine);
    opt_set_lighting(M.lighting);
    opt_set_touch_controls(M.touch_controls);
    hd2d_light_set_angles((float)M.light_angle, (float)M.light_height);
    opt_set_sprite_chain(&M.chain);
    /* The slider edits a percentage; the light wants the multiplier itself. */
    opt_set_light_intensity((float)M.light_pct / 100.0f);
}

static void activate_focused(bool controller)
{
    g_dispatching_pad = controller;
    if (Rml::Element *focused = g_ctx->GetFocusElement()) focused->Click();
    g_dispatching_pad = false;
}

static void cancel_document()
{
    pause_menu_close();
}

/* ---- the C API ---- */

int rmlui_init(SDL_Renderer *r, SDL_Window *w)
{
    if (g_ctx) return 1;
    if (!r) return 0;
    g_R = r;
    g_W = w;
    rmlui_system_interface().SetWindow(w);
    g_render = new RmlUiRenderBackend(r);
    Rml::SetSystemInterface(&rmlui_system_interface());
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
    /* Dusklight uses the display's expected CONTENT scale for dp layout and FreeType raster
     * size. Pixel density only maps window points to drawable pixels and can be 1 on an X11
     * desktop whose content scale is 2. */
    g_ctx->SetDensityIndependentPixelRatio(content_scale());

    auto ctor = g_ctx->CreateDataModel("settings");
    if (!ctor) {
        fprintf(stderr, "rmlui: CreateDataModel failed\n");
        return 0;
    }
    data_model() = ctor.GetModelHandle();
    ctor.Bind("engine", &M.engine);
    ctor.Bind("lighting", &M.lighting);
    ctor.Bind("touch_controls", &M.touch_controls);
    ctor.Bind("in_match", &M.in_match);
    ctor.Bind("can_drop", &M.can_drop);
    ctor.Bind("light_angle", &M.light_angle);
    ctor.Bind("light_height", &M.light_height);
    ctor.Bind("light_pct", &M.light_pct);
    ctor.Bind("pass_count", &M.pass_count);
    ctor.Bind("pass_max", &M.pass_max);
    ctor.Bind("aa", &M.aa);
    ctor.Bind("inner", &M.inner);
    ctor.Bind("outline_name", &M.outline_name);
    for (int i = 0; i < SPRITE_PASS_MAX; i++) {
        ctor.Bind("pass_kind" + std::to_string(i), &M.pass_kind[i]);
        ctor.Bind("pass_factor" + std::to_string(i), &M.pass_factor[i]);
    }
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
        rmlui_input_block_until_release();
        M.pad_name[b] = g_pad_capture_armed ? "PRESS BUTTON" : "RELEASE BUTTON";
        data_model().DirtyVariable(std::string("pad_") + binding_action_id(b));
    });
    ctor.BindEventCallback("show_page", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        M.page = args[0].Get<Rml::String>();
        data_model().DirtyVariable("page");
        if (getenv("LF2_RMLUI_DEBUG")) fprintf(stderr, "rmlui page: %s\n", M.page.c_str());
    });
    ctor.BindEventCallback("toggle_engine", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        M.engine = !M.engine;
        data_model().DirtyVariable("engine");
    });
    ctor.BindEventCallback("toggle_lighting", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        M.lighting = !M.lighting;
        data_model().DirtyVariable("lighting");
    });
    ctor.BindEventCallback("toggle_touch_controls", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        M.touch_controls = !M.touch_controls;
        data_model().DirtyVariable("touch_controls");
    });
    /* THE PASS ROWS. Every one of these steps a value and asks spritefilter.h to keep the
     * chain legal -- the menu holds no rule of its own, so a cap or a spelling can change in
     * one place and the menu follows. */
    ctor.BindEventCallback("cycle_pass_kind", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        spritechain_cycle_kind(&M.chain, std::atoi(args[0].Get<Rml::String>().c_str()));
        refresh_sprite_rows();
    });
    ctor.BindEventCallback("cycle_pass_factor", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        const int i = std::atoi(args[0].Get<Rml::String>().c_str());
        if (i < 0 || i >= M.chain.count) return;
        spritechain_cycle_factor(&M.chain.pass[i]);
        refresh_sprite_rows();
    });
    ctor.BindEventCallback("remove_pass", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        spritechain_remove_pass(&M.chain, std::atoi(args[0].Get<Rml::String>().c_str()));
        refresh_sprite_rows();
    });
    ctor.BindEventCallback("add_pass", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        spritechain_add_pass(&M.chain);
        refresh_sprite_rows();
    });
    ctor.BindEventCallback("toggle_aa", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        M.chain.smooth = !M.chain.smooth;
        refresh_sprite_rows();
    });
    ctor.BindEventCallback("toggle_inner", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        M.chain.inner = !M.chain.inner;
        refresh_sprite_rows();
    });
    ctor.BindEventCallback("cycle_outline", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        M.chain.outline = M.chain.outline >= SPRITE_OUTLINE_MAX ? 0 : M.chain.outline + 1;
        refresh_sprite_rows();
    });
    ctor.BindEventCallback("activate_cheat", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (args.empty()) return;
        CheatAction action;
        if (!cheats_action_from_id(args[0].Get<Rml::String>().c_str(), &action)) return;
        size_t count = 0;
        const CheatDescriptor *descriptors = cheats_descriptors(&count);
        for (size_t i = 0; i < count; ++i)
            if (descriptors[i].action == action && descriptors[i].match_only && !M.in_match) return;
        pause_menu_close();
        if (!cheats_request(action)) fprintf(stderr, "cheat command: another function-key pulse is active\n");
    });
    ctor.BindEventCallback("close",
                           [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { pause_menu_close(); });
    ctor.BindEventCallback("drop_out",
                           [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { pause_menu_drop_out(); });
    ctor.BindEventCallback(
        "leave_match", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { pause_menu_leave_match(); });
    ctor.BindEventCallback(
        "quit", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { hostwin_request_quit(); });

    const std::string document = settings_document();
    g_doc = g_ctx->LoadDocumentFromMemory(document, "settings");
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
    rmlui_lifecycle_init(&g_lifecycle);
    g_key_capture = g_pad_capture = -1;
    rmlui_input_reset();
    g_metrics_reported = 0;
    g_open_count = g_render_frames = 0;
}

int rmlui_active(void)
{
    return g_lifecycle.active;
}

void rmlui_open(void)
{
    if (!g_ctx) return;
    rmlui_lifecycle_open(&g_lifecycle);
    g_open_count++;
    g_key_capture = g_pad_capture = -1;
    rmlui_input_reset();
    model_load();
    if (g_doc) {
        g_doc->Show();
        if (Rml::Element *first = g_doc->GetElementById("continue")) first->Focus();
    }
}

void rmlui_close(void)
{
    if (!g_lifecycle.active) return;
    rmlui_lifecycle_close(&g_lifecycle);
    g_key_capture = g_pad_capture = -1;
    model_store();
    if (g_doc) g_doc->Hide();
}

void rmlui_render(void)
{
    if (!g_ctx || !g_render) return;
    const unsigned frame = rmlui_lifecycle_frame_begin(&g_lifecycle);
    if (!frame) return;
    /* Values are stored immediately; the game keeps its ordinary update/draw/present path
     * behind this document, so renderer and lighting changes remain live. */
    model_store_live();

    /* The context follows the render output, so the panel stays centred however the window
     * is sized or resized. */
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(g_R, &ow, &oh);
    if (ow > 0 && oh > 0) {
        const Rml::Vector2i d = g_ctx->GetDimensions();
        if (d.x != ow || d.y != oh) g_ctx->SetDimensions(Rml::Vector2i(ow, oh));
    }
    const float display_scale = content_scale();
    if (display_scale != g_ctx->GetDensityIndependentPixelRatio())
        g_ctx->SetDensityIndependentPixelRatio(display_scale);

    const RmlUiInputCallbacks callbacks = {activate_focused, cancel_document};
    if (g_doc) rmlui_input_update(*g_ctx, *g_doc, callbacks);
    /* Cancel/Continue is allowed to close the document from rmlui_input_update.  That callback
     * invalidates this UI frame: updating or rendering it after Hide() produced the one-frame
     * close glitch from issue #94. */
    if (!rmlui_lifecycle_frame_continues(&g_lifecycle, frame)) return;
    g_ctx->Update();
    if (!rmlui_lifecycle_frame_continues(&g_lifecycle, frame)) return;
    g_render_frames++;
    g_render->BeginFrame();
    if (!g_metrics_reported && getenv("LF2_RMLUI_DEBUG")) {
        int window_w = 0, window_h = 0;
        int pixel_w = 0, pixel_h = 0;
        SDL_GetWindowSize(g_W, &window_w, &window_h);
        SDL_GetRenderOutputSize(g_R, &pixel_w, &pixel_h);
        fprintf(stderr,
                "rmlui metrics: %dx%d window points -> %dx%d drawable pixels, content scale "
                "%.2f, body font %.1fpx\n",
                window_w, window_h, pixel_w, pixel_h, display_scale,
                g_doc ? static_cast<double>(g_doc->GetComputedValues().font_size()) : 0.0);
        g_metrics_reported = 1;
    }
    g_ctx->Render();
    g_render->EndFrame();
}

int rmlui_event(SDL_Event *e)
{
    if (!g_ctx || !g_lifecycle.active) return 0;

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
            rmlui_input_block_until_release();
        }
        return 1; /* the binding is ours, not the game's */
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
            rmlui_input_block_until_release();
        }
        return 1;
    }

    /* Escape's physical state is the one toggle owned by pause_tick. Consume the SDL event,
     * but do not close here: closing during the pump and then edge-polling the same held key
     * would immediately reopen the document in the game update that follows. */
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_ESCAPE) {
        return 1;
    }

    if (rmlui_input_note_event(*e)) return 1;

    /* The pinned SDL backend predates SDL_EVENT_FINGER_CANCELED even though this RmlUi
     * revision has an explicit cancellation API. Forward it here so Android lifecycle
     * cancellation cannot leave a pressed element or captured touch behind. */
    if (e->type == SDL_EVENT_FINGER_CANCELED) {
        const Rml::Vector2i dimensions = g_ctx->GetDimensions();
        const Rml::Vector2f position = {
            e->tfinger.x * static_cast<float>(dimensions.x),
            e->tfinger.y * static_cast<float>(dimensions.y),
        };
        const Rml::TouchList touches = {Rml::Touch{static_cast<Rml::TouchId>(e->tfinger.fingerID), position}};
        g_ctx->ProcessTouchCancel(touches);
        return 1;
    }

    /* Conventional raw keyboard activation remains available even when Return and Space are
     * not player-action bindings. Configured actions take the device-independent path above. */
    if (e->type == SDL_EVENT_KEY_DOWN &&
        (e->key.scancode == SDL_SCANCODE_RETURN || e->key.scancode == SDL_SCANCODE_SPACE)) {
        activate_focused(false);
        return 1;
    }
    if (rmlui_input_pointer_event(*g_ctx, *g_R, *e)) return 1;

    SDL_Event copy = *e;
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
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    default: return 0;
    }
}
