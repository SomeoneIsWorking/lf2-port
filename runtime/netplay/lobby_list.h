#ifndef LF2_NETPLAY_LOBBY_LIST_H
#define LF2_NETPLAY_LOBBY_LIST_H

#include <lucent/p2p/directory.h>
#include <lucent/p2p/lobby_seats.h>
#include <lucent/worker.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace lf2::netplay {

// The open games across every directory, on a thread of its own. A host's game stays listed while
// it is offered and each player who asks gets its invite; a player asks a listed game for a seat
// and gets that invite back. Every call returns at once.
class LobbyList {
  public:
    struct Offer {
        lucent::p2p::Lobby lobby;
        std::string invite;
    };
    enum class Seat { Waiting, Given, Full, Unanswered, Failed };
    struct SeatAnswer {
        Seat seat = Seat::Waiting;
        std::string invite;
    };

    explicit LobbyList(std::vector<std::unique_ptr<lucent::p2p::Directory>> directories);
    ~LobbyList();
    LobbyList(const LobbyList &) = delete;
    LobbyList &operator=(const LobbyList &) = delete;
    LobbyList(LobbyList &&) = delete;
    LobbyList &operator=(LobbyList &&) = delete;

    // Listed until cleared; a new lobby id lists again under a new contact.
    void offer(std::optional<Offer> offer);
    void refresh();
    [[nodiscard]] std::vector<lucent::p2p::Lobby> lobbies() const;
    // Asks the host of a listed game for a seat; the answer comes through `seat`.
    void ask(const lucent::p2p::Lobby &lobby);
    // The current ask's answer; Given, Full, Unanswered and Failed are handed out once.
    [[nodiscard]] std::optional<SeatAnswer> seat();

  private:
    struct Listed {
        lucent::p2p::Lobby lobby;
        std::size_t directory = 0;
    };
    struct Asking {
        std::unique_ptr<lucent::p2p::LobbyGuest> guest;
        std::chrono::steady_clock::time_point since;
    };

    void run(const lucent::StopToken &stop);
    void keep_offer(const std::optional<Offer> &offer);
    void answer_asks(const Offer &offer);
    void withdraw_offer();
    void list_now();
    void start_ask(const lucent::p2p::Lobby &lobby);
    void poll_ask();
    void settle(SeatAnswer answer);

    std::vector<std::unique_ptr<lucent::p2p::Directory>> directories_;
    // Worker thread only.
    std::vector<std::unique_ptr<lucent::p2p::LobbyHost>> hosts_;
    std::optional<Offer> offered_;
    std::optional<Asking> asking_;
    std::chrono::steady_clock::time_point asks_read_{};

    mutable std::mutex mutex_;
    std::optional<Offer> wanted_;
    bool refresh_wanted_ = false;
    std::optional<lucent::p2p::Lobby> ask_wanted_;
    std::vector<Listed> listed_;
    std::optional<SeatAnswer> seat_;
    lucent::Worker worker_;
};

} // namespace lf2::netplay

#endif
