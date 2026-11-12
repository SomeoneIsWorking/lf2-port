#ifndef LF2_UI_ONLINE_UI_H
#define LF2_UI_ONLINE_UI_H

#include <RmlUi/Core.h>
#include <lucent/p2p/directory.h>

#include <chrono>
#include <string>
#include <vector>

namespace lf2::ui {

// The port menu's ONLINE page: host a game, copy this player's code, paste a friend's, list the
// game, and join one of the open games. The session itself is runtime/netplay's.
class OnlinePage {
  public:
    static constexpr int kLobbyRows = 6;

    // The tab and the page, for the settings document's markers.
    [[nodiscard]] static std::string tab_markup();
    [[nodiscard]] static std::string page_markup();
    void bind(Rml::DataModelConstructor &model);
    // Every frame the menu is up, with the page the menu shows.
    void update(Rml::DataModelHandle model, const Rml::String &page);

  private:
    void copy_code();
    void paste_code();
    void join(int row);

    Rml::String status_;
    Rml::String host_label_;
    Rml::String copy_label_;
    Rml::String paste_label_;
    Rml::String lobbies_label_;
    bool listed_ = false;
    bool active_ = false;
    int lobby_count_ = 0;
    Rml::String lobby_names_[kLobbyRows];
    std::vector<lucent::p2p::Lobby> lobbies_;
    std::string copied_;
    std::string pasted_problem_;
    std::chrono::steady_clock::time_point refreshed_{};
};

} // namespace lf2::ui

#endif
