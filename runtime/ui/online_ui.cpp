#include "online_ui.h"

#include "online_session.h"
#include "options.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <string_view>

namespace lf2::ui {
namespace {

using netplay::OnlineView;
using Stage = OnlineView::Stage;

// While the page is open, the open games are fetched again this often.
constexpr std::chrono::seconds kRefreshEvery{10};

std::string clipboard_text()
{
    char *text = SDL_GetClipboardText();
    std::string out = text ? text : "";
    SDL_free(text);
    out.erase(std::remove_if(out.begin(), out.end(), [](char c) { return c == ' ' || c == '\n' || c == '\r'; }),
              out.end());
    return out;
}

Rml::String status_text(const OnlineView &view, bool copied)
{
    switch (view.stage) {
    case Stage::Idle: return "Host a game, or paste a friend's code";
    case Stage::Asking: return "Asking the host for a seat...";
    case Stage::Starting: return "Making a code...";
    case Stage::Inviting: return copied ? "Code copied: send it to a friend" : "Copy your code and send it to a friend";
    case Stage::Joining: return "Reaching the host...";
    case Stage::Replying:
        return view.host ? "Paste the reply your friend sends back" : "Copy your reply code and send it to the host";
    case Stage::Syncing: return "Joining the game...";
    case Stage::Playing: return view.host ? "Playing online as player 1" : "Playing online as player 2";
    case Stage::Failed: return view.problem;
    }
    return {};
}

} // namespace

std::string OnlinePage::tab_markup()
{
    return R"RML(<button data-event-click="show_page('online')">ONLINE</button>)RML";
}

std::string OnlinePage::page_markup()
{
    std::string rows;
    for (int i = 0; i < kLobbyRows; ++i) {
        const std::string n = std::to_string(i);
        rows += R"RML(<div class="setting-row" data-if="online_lobby_count > )RML" + n +
                R"RML("><span class="label">{{online_lobby)RML" + n +
                R"RML(}}</span><button class="setting-value" data-event-click="online_join(')RML" + n +
                R"RML(')">JOIN</button></div>)RML";
    }
    return R"RML(<pane data-if="page == 'online'">
        <span class="section-heading">PLAY ONLINE</span>
        <div class="setting-row"><span class="label">{{online_status}}</span></div>
        <div class="setting-row"><span class="label">Host a game</span><button id="online-host" class="setting-value" data-event-click="online_host">{{online_host_label}}</button></div>
        <div class="setting-row"><span class="label">Your code</span><button id="online-copy" class="setting-value" data-event-click="online_copy">{{online_copy_label}}</button></div>
        <div class="setting-row"><span class="label">Join by code</span><button id="online-paste" class="setting-value" data-event-click="online_paste">{{online_paste_label}}</button></div>
        <div class="setting-row"><span class="label">List my game in open games</span><button class="setting-value" data-event-click="online_toggle_listed">{{online_listed ? 'ON' : 'OFF'}}</button></div>
        <div class="setting-row" data-if="online_listed"><span class="label">Anyone who asks to join a listed game learns your address</span></div>
        <button data-if="online_active" data-event-click="online_leave">LEAVE ONLINE GAME</button>
        <span class="section-heading">OPEN GAMES</span>
        <div class="setting-row"><span class="label">{{online_lobbies_label}}</span><button class="setting-value" data-event-click="online_refresh">REFRESH</button></div>
        )RML" +
           rows +
           R"RML(
      </pane>)RML";
}

void OnlinePage::bind(Rml::DataModelConstructor &model)
{
    model.Bind("online_status", &status_);
    model.Bind("online_host_label", &host_label_);
    model.Bind("online_copy_label", &copy_label_);
    model.Bind("online_paste_label", &paste_label_);
    model.Bind("online_lobbies_label", &lobbies_label_);
    model.Bind("online_listed", &listed_);
    model.Bind("online_active", &active_);
    model.Bind("online_lobby_count", &lobby_count_);
    for (int i = 0; i < kLobbyRows; ++i) {
        model.Bind("online_lobby" + std::to_string(i), &lobby_names_[i]);
    }
    model.BindEventCallback("online_host", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        copied_.clear();
        pasted_problem_.clear();
        netplay::online_host();
    });
    model.BindEventCallback("online_copy",
                            [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { copy_code(); });
    model.BindEventCallback("online_paste",
                            [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { paste_code(); });
    model.BindEventCallback(
        "online_toggle_listed",
        [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { netplay::online_set_listed(!listed_); });
    model.BindEventCallback("online_leave", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        copied_.clear();
        pasted_problem_.clear();
        netplay::online_leave();
    });
    model.BindEventCallback("online_refresh", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        refreshed_ = std::chrono::steady_clock::now();
        netplay::online_refresh_lobbies();
    });
    model.BindEventCallback("online_join", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
        if (!args.empty()) {
            join(std::stoi(args[0].Get<Rml::String>()));
        }
    });
}

void OnlinePage::copy_code()
{
    const OnlineView view = netplay::online_view();
    if (view.code.empty()) {
        return;
    }
    if (SDL_SetClipboardText(view.code.c_str())) {
        copied_ = view.code;
    } else {
        pasted_problem_ = "The clipboard refused the code";
    }
}

void OnlinePage::paste_code()
{
    pasted_problem_ = netplay::online_paste(clipboard_text());
}

void OnlinePage::join(int row)
{
    if (row < 0 || row >= static_cast<int>(lobbies_.size())) {
        return;
    }
    copied_.clear();
    pasted_problem_.clear();
    netplay::online_join(lobbies_[static_cast<std::size_t>(row)]);
}

void OnlinePage::update(Rml::DataModelHandle model, const Rml::String &page)
{
    const OnlineView view = netplay::online_view();
    const bool copied = !view.code.empty() && view.code == copied_;
    Rml::String status = pasted_problem_.empty() ? status_text(view, copied) : Rml::String{pasted_problem_};
    const bool meeting = view.stage == Stage::Starting || view.stage == Stage::Inviting ||
                         view.stage == Stage::Joining || view.stage == Stage::Replying;
    const Rml::String host_label = view.host && meeting ? "HOSTING" : "HOST";
    const Rml::String copy_label = view.code.empty() ? "-" : copied ? "COPIED" : "COPY";
    const Rml::String paste_label = view.host && meeting ? "PASTE REPLY" : "PASTE";
    const bool active = view.stage != Stage::Idle && view.stage != Stage::Failed;
    const bool listed = opt_online_listed() != 0;

    const auto now = std::chrono::steady_clock::now();
    if (page == "online" && now - refreshed_ >= kRefreshEvery) {
        refreshed_ = now;
        netplay::online_refresh_lobbies();
    }
    lobbies_ = netplay::online_lobbies();
    const int count = std::min(static_cast<int>(lobbies_.size()), kLobbyRows);
    for (int i = 0; i < count; ++i) {
        const lucent::p2p::Lobby &lobby = lobbies_[static_cast<std::size_t>(i)];
        const Rml::String name = lobby.title + " (" + lobby.host.substr(0, 8) + ")";
        if (lobby_names_[i] != name) {
            lobby_names_[i] = name;
            model.DirtyVariable("online_lobby" + std::to_string(i));
        }
    }
    const Rml::String lobbies_label = count == 0 ? "No open games yet" : std::to_string(count) + " open";

    const auto set = [&model](auto &field, const auto &value, const char *name) {
        if (field != value) {
            field = value;
            model.DirtyVariable(name);
        }
    };
    set(status_, status, "online_status");
    set(host_label_, host_label, "online_host_label");
    set(copy_label_, copy_label, "online_copy_label");
    set(paste_label_, paste_label, "online_paste_label");
    set(lobbies_label_, lobbies_label, "online_lobbies_label");
    set(active_, active, "online_active");
    set(lobby_count_, count, "online_lobby_count");
    set(listed_, listed, "online_listed");
}

} // namespace lf2::ui
