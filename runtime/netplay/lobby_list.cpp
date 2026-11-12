#include "lobby_list.h"

#include "lf2_log.h"

#include <algorithm>
#include <utility>

namespace lf2::netplay {
namespace {

using namespace std::chrono_literals;
using lucent::p2p::Lobby;

// How often a host reads its asks, and a player looks for the host's answer.
constexpr auto kReadEvery = 1s;
// A host that has not answered by then is taken to be gone.
constexpr auto kAnswerWithin = 30s;

} // namespace

LobbyList::LobbyList(std::vector<std::unique_ptr<lucent::p2p::Directory>> directories)
    : directories_(std::move(directories)), worker_([this](const lucent::StopToken &stop) { run(stop); })
{
}

LobbyList::~LobbyList() = default;

void LobbyList::offer(std::optional<Offer> offer)
{
    const std::lock_guard lock{mutex_};
    wanted_ = std::move(offer);
}

void LobbyList::refresh()
{
    const std::lock_guard lock{mutex_};
    refresh_wanted_ = true;
}

std::vector<Lobby> LobbyList::lobbies() const
{
    const std::lock_guard lock{mutex_};
    std::vector<Lobby> open;
    open.reserve(listed_.size());
    for (const Listed &listed : listed_) {
        open.push_back(listed.lobby);
    }
    return open;
}

void LobbyList::ask(const Lobby &lobby)
{
    const std::lock_guard lock{mutex_};
    ask_wanted_ = lobby;
    seat_ = SeatAnswer{};
}

std::optional<LobbyList::SeatAnswer> LobbyList::seat()
{
    const std::lock_guard lock{mutex_};
    std::optional<SeatAnswer> answer = seat_;
    if (seat_ && seat_->seat != Seat::Waiting) {
        seat_.reset();
    }
    return answer;
}

void LobbyList::run(const lucent::StopToken &stop)
{
    while (!stop.sleep_for(100ms)) {
        std::optional<Offer> wanted;
        bool refresh = false;
        std::optional<Lobby> ask;
        {
            const std::lock_guard lock{mutex_};
            wanted = wanted_;
            refresh = std::exchange(refresh_wanted_, false);
            ask = std::exchange(ask_wanted_, std::nullopt);
        }
        keep_offer(wanted);
        if (refresh) {
            list_now();
        }
        if (ask) {
            start_ask(*ask);
        }
        poll_ask();
    }
    withdraw_offer();
}

void LobbyList::keep_offer(const std::optional<Offer> &offer)
{
    if (offered_ && (!offer || offer->lobby.id != offered_->lobby.id)) {
        withdraw_offer();
    }
    if (!offer) {
        return;
    }
    if (hosts_.empty()) {
        for (const std::unique_ptr<lucent::p2p::Directory> &directory : directories_) {
            hosts_.push_back(std::make_unique<lucent::p2p::LobbyHost>(*directory, offer->lobby));
        }
    }
    offered_ = offer;
    for (const std::unique_ptr<lucent::p2p::LobbyHost> &host : hosts_) {
        std::string error;
        if (!host->keep_listed(error)) {
            lf2_log_writef(LF2_LOG_WARNING, "lobby", "a directory did not list the game: %s", error.c_str());
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - asks_read_ >= kReadEvery) {
        asks_read_ = now;
        answer_asks(*offer);
    }
}

void LobbyList::answer_asks(const Offer &offer)
{
    for (const std::unique_ptr<lucent::p2p::LobbyHost> &host : hosts_) {
        std::string error;
        const std::optional<std::vector<lucent::p2p::LobbyHost::Asker>> askers = host->asks(error);
        if (!askers) {
            lf2_log_writef(LF2_LOG_WARNING, "lobby", "a directory did not give the asks: %s", error.c_str());
            continue;
        }
        for (const lucent::p2p::LobbyHost::Asker &asker : *askers) {
            if (host->answer(asker, offer.invite, error)) {
                lf2_log_write(LF2_LOG_INFO, "lobby", "answered a player who asked for a seat");
            } else {
                lf2_log_writef(LF2_LOG_WARNING, "lobby", "an answer was not sent: %s", error.c_str());
            }
        }
    }
}

void LobbyList::withdraw_offer()
{
    for (const std::unique_ptr<lucent::p2p::LobbyHost> &host : hosts_) {
        std::string error;
        if (!host->withdraw(error)) {
            lf2_log_writef(LF2_LOG_WARNING, "lobby", "a directory kept the game listed: %s", error.c_str());
        }
    }
    hosts_.clear();
    offered_.reset();
}

void LobbyList::list_now()
{
    std::vector<Listed> merged;
    for (std::size_t index = 0; index < directories_.size(); ++index) {
        std::string error;
        std::optional<std::vector<Lobby>> listed = directories_[index]->list(error);
        if (!listed) {
            lf2_log_writef(LF2_LOG_WARNING, "lobby", "a directory did not answer: %s", error.c_str());
            continue;
        }
        for (Lobby &lobby : *listed) {
            const bool seen = std::any_of(merged.begin(), merged.end(),
                                          [&](const Listed &each) { return each.lobby.id == lobby.id; });
            if (!seen) {
                merged.push_back(Listed{std::move(lobby), index});
            }
        }
    }
    const std::lock_guard lock{mutex_};
    listed_ = std::move(merged);
}

void LobbyList::start_ask(const Lobby &lobby)
{
    asking_.reset();
    std::optional<std::size_t> directory;
    {
        const std::lock_guard lock{mutex_};
        const auto found = std::find_if(listed_.begin(), listed_.end(), [&](const Listed &each) {
            return each.lobby.id == lobby.id && each.lobby.contact == lobby.contact;
        });
        if (found != listed_.end()) {
            directory = found->directory;
        }
    }
    if (!directory) {
        settle(SeatAnswer{Seat::Failed, {}});
        return;
    }
    auto guest = std::make_unique<lucent::p2p::LobbyGuest>(*directories_[*directory], lobby);
    std::string error;
    if (!guest->ask(error)) {
        lf2_log_writef(LF2_LOG_WARNING, "lobby", "the ask was not sent: %s", error.c_str());
        settle(SeatAnswer{Seat::Failed, {}});
        return;
    }
    lf2_log_write(LF2_LOG_INFO, "lobby", "asked a listed game for a seat");
    asking_ = Asking{std::move(guest), std::chrono::steady_clock::now()};
}

void LobbyList::poll_ask()
{
    if (!asking_) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - asking_->since >= kAnswerWithin) {
        asking_.reset();
        settle(SeatAnswer{Seat::Unanswered, {}});
        return;
    }
    std::string error;
    const std::optional<lucent::p2p::LobbyGuest::Answer> answer = asking_->guest->answer(error);
    if (!answer) {
        if (!error.empty()) {
            lf2_log_writef(LF2_LOG_WARNING, "lobby", "no answer yet: %s", error.c_str());
        }
        return;
    }
    asking_.reset();
    settle(answer->invite ? SeatAnswer{Seat::Given, *answer->invite} : SeatAnswer{Seat::Full, {}});
}

void LobbyList::settle(SeatAnswer answer)
{
    const std::lock_guard lock{mutex_};
    seat_ = std::move(answer);
}

} // namespace lf2::netplay
