#ifndef LF2_NETPLAY_ONLINE_SESSION_H
#define LF2_NETPLAY_ONLINE_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Two LF2s meeting by a code over Lucent's peer-to-peer transport, the friend's game taking the
 * host's state, then both playing every frame through Lucent's rollback engine. The main loop
 * drives it between frames. */
void lf2_online_poll(void);
/* The game must not run a frame: a friend is taking this game's state, or this game the host's. */
int lf2_online_holding(void);
int lf2_online_playing(void);
/* The session's next logic frame, rerunning earlier ones when a late input changed them and waiting
 * while the peer is too far behind. 0 once a quit came first. */
int lf2_online_advance(void);
void lf2_online_shutdown(void);

#ifdef __cplusplus
}

#include <lucent/p2p/directory.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lf2::netplay {

struct OnlineView {
    enum class Stage : std::uint8_t {
        Idle,
        // A listed game's host is being asked for a seat.
        Asking,
        // The code is being made.
        Starting,
        // The host's code is ready; nobody has arrived.
        Inviting,
        // The friend is reaching the host.
        Joining,
        // The friend's reply code is ready for the host to paste.
        Replying,
        // Both are through; the friend's game is taking the host's.
        Syncing,
        Playing,
        Failed,
    };
    Stage stage = Stage::Idle;
    bool host = false;
    // The code this player sends: the host's invite, or the friend's reply.
    std::string code;
    // Why the last attempt ended.
    std::string problem;
    // This player's game is listed in the open games.
    bool listed = false;
};

[[nodiscard]] OnlineView online_view();
void online_host();
// A pasted code: a host's invite to join, or a friend's reply to this host's invite. Empty when
// taken, else what is wrong with it.
[[nodiscard]] std::string online_paste(std::string_view code);
void online_leave();
// Whether a hosted game goes on the open games list.
void online_set_listed(bool listed);
// The open games, refreshed while asked for.
[[nodiscard]] std::vector<lucent::p2p::Lobby> online_lobbies();
// Asks the host of one of `online_lobbies` for a seat, then joins by the invite it answers with.
void online_join(const lucent::p2p::Lobby &lobby);
void online_refresh_lobbies();

} // namespace lf2::netplay
#endif

#endif
