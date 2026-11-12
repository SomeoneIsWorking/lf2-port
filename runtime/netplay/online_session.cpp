#include "online_session.h"

#include "environment.h"
#include "game_runtime.h"
#include "hostwin.h"
#include "lf2_log.h"
#include "lobby_list.h"
#include "main_loop.h"
#include "options.h"
#include "packet.h"
#include "rmlui.h"
#include "session_inputs.h"
#include "user_paths.h"

#include <lucent/content.h>
#include <lucent/p2p/connector.h>
#include <lucent/p2p/crypto.h>
#include <lucent/p2p/http_directory.h>
#include <lucent/p2p/nostr_directory.h>
#include <lucent/p2p/reliable_link.h>
#include <lucent/p2p/zerotier.h>
#include <lucent/platform.h>
#include <lucent/rollback/session.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace lf2::netplay {
namespace {

using namespace std::chrono_literals;
using lucent::p2p::Connector;
using lucent::p2p::Role;

constexpr lucent::p2p::CodePrefixes kPrefixes{"LF2", "LFR"};
constexpr std::string_view kKeyLabel = "lf2-online";
constexpr std::string_view kSessionPurpose = "lf2-session";
constexpr std::string_view kDirectoryGame = "lf2";
// LF2's port, which names its ZeroTier ad-hoc network.
constexpr std::uint16_t kZeroTierPort = 41702;
constexpr std::uint8_t kHelloKind = 1;
constexpr std::uint8_t kRollbackKind = 3;
constexpr std::uint8_t kSyncKind = 4;
constexpr std::size_t kRollbackMessageBytes = 4096;
constexpr std::size_t kSyncMessageBytes = std::size_t{4} << 20;
// Pages per sync message, so one stays well under its cap.
constexpr std::size_t kPagesPerMessage = 128;
// A final hash is logged this often, for a check that both peers played the same frames.
constexpr std::int64_t kHashReportFrames = 60;

// What the host and the friend say while the friend's game takes the host's.
enum class SyncStep : std::uint8_t {
    // Host: its state image, then every logic page's address and hash.
    Offer = 1,
    // Friend: the pages whose hash differs from its own.
    Want = 2,
    // Host: those pages, a message at a time.
    Pages = 3,
    // Friend: its state hash once every page is in.
    Loaded = 4,
    // Host: the two hashes agree, so both play.
    Play = 5,
    // Either: why the session cannot start.
    Refuse = 6,
};

lucent::p2p::LinkCaps link_caps()
{
    lucent::p2p::LinkCaps caps{};
    caps[kHelloKind] = LF2_NETPLAY_HELLO_BYTES;
    caps[kRollbackKind] = kRollbackMessageBytes;
    caps[kSyncKind] = kSyncMessageBytes;
    return caps;
}

class Writer {
  public:
    explicit Writer(SyncStep step)
    {
        out_.push_back(static_cast<char>(step));
    }
    template <typename T> void put(const T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        out_.append(reinterpret_cast<const char *>(&value), sizeof value);
    }
    void put_bytes(std::span<const std::uint8_t> bytes)
    {
        out_.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    [[nodiscard]] const std::string &body() const noexcept
    {
        return out_;
    }

  private:
    std::string out_;
};

class Reader {
  public:
    explicit Reader(std::string_view in) : in_(in)
    {
    }
    template <typename T> bool get(T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (in_.size() - at_ < sizeof value) {
            return false;
        }
        std::memcpy(&value, in_.data() + at_, sizeof value);
        at_ += sizeof value;
        return true;
    }
    std::optional<std::span<const std::uint8_t>> get_bytes(std::size_t size)
    {
        if (in_.size() - at_ < size) {
            return std::nullopt;
        }
        const std::span<const std::uint8_t> bytes{reinterpret_cast<const std::uint8_t *>(in_.data() + at_), size};
        at_ += size;
        return bytes;
    }

  private:
    std::string_view in_;
    std::size_t at_ = 0;
};

std::string encode(const Lf2NetplayHello &hello)
{
    std::array<std::uint8_t, LF2_NETPLAY_HELLO_BYTES> bytes{};
    const std::size_t size = lf2_netplay_hello_encode(&hello, bytes.data(), bytes.size());
    return {reinterpret_cast<const char *>(bytes.data()), size};
}

std::string connector_problem(Connector::Failure failure)
{
    switch (failure) {
    case Connector::Failure::Unreachable: return "No way through to the other player";
    case Connector::Failure::Lapsed: return "The code ran out before anyone came";
    case Connector::Failure::NoNetwork: return "No network";
    case Connector::Failure::None: break;
    }
    return "The meeting failed";
}

std::string code_problem(lucent::p2p::CodeProblem problem)
{
    switch (problem) {
    case lucent::p2p::CodeProblem::Empty: return "Copy a code first";
    case lucent::p2p::CodeProblem::Unknown: return "Not a Little Fighter 2 code";
    case lucent::p2p::CodeProblem::WrongKind: return "That is an invite; paste the friend's reply instead";
    case lucent::p2p::CodeProblem::Damaged: return "A letter is wrong or missing";
    case lucent::p2p::CodeProblem::Version: return "The code is from another version";
    case lucent::p2p::CodeProblem::OtherInvite: return "A reply to another invite";
    }
    return "Not a code";
}

std::optional<std::string> read_file(const char *path)
{
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, {}};
}

std::unique_ptr<lucent::p2p::ZeroTierNode> start_zerotier()
{
    char storage[1024];
    char error[256];
    if (!user_paths_zerotier_directory(storage, sizeof storage, error, sizeof error)) {
        lf2_log_writef(LF2_LOG_WARNING, "online", "no ZeroTier: %s; meeting direct only", error);
        return nullptr;
    }
    lucent::p2p::ZeroTierConfig config{.storage = storage, .roots = {}, .port = 0, .network_port = kZeroTierPort};
    if (const char *roots = lf2_environment_get(LF2_ENV_ZEROTIER_ROOTS)) {
        std::optional<std::string> set = read_file(roots);
        if (!set) {
            lf2_log_writef(LF2_LOG_WARNING, "online", "cannot read the ZeroTier root set %s; meeting direct only",
                           roots);
            return nullptr;
        }
        config.roots = std::move(*set);
    }
    std::string why;
    std::unique_ptr<lucent::p2p::ZeroTierNode> node = lucent::p2p::ZeroTierNode::start(config, why);
    if (!node) {
        lf2_log_writef(LF2_LOG_WARNING, "online", "no ZeroTier: %s; meeting direct only", why.c_str());
    }
    return node;
}

std::vector<std::unique_ptr<lucent::p2p::Directory>> directories()
{
    std::vector<std::unique_ptr<lucent::p2p::Directory>> list;
    const std::string game{kDirectoryGame};
    if (const char *server = opt_lobby_server(); server && *server) {
        try {
            list.push_back(std::make_unique<lucent::p2p::HttpDirectory>(game, server, lucent::p2p::TlsTrust{}));
        } catch (const std::invalid_argument &problem) {
            lf2_log_writef(LF2_LOG_WARNING, "lobby", "the lobby server %s is not usable: %s", server, problem.what());
        }
    }
    if (opt_nostr_lobbies()) {
        std::vector<std::string> relays{lucent::p2p::kDefaultNostrRelays.begin(),
                                        lucent::p2p::kDefaultNostrRelays.end()};
        list.push_back(std::make_unique<lucent::p2p::NostrDirectory>(game, std::move(relays), lucent::p2p::TlsTrust{}));
    }
    return list;
}

std::string hex(std::span<const std::uint8_t> bytes)
{
    static constexpr std::string_view kDigits = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t byte : bytes) {
        out.push_back(kDigits[byte >> 4U]);
        out.push_back(kDigits[byte & 15U]);
    }
    return out;
}

// One attempt, from the code to the end of play: the meeting, the hello, the friend's game taking
// the host's, then every frame through the rollback engine.
class Attempt {
  public:
    Attempt(const lucent::content::Sha256 &executable, const lucent::content::Sha256 &port,
            std::unique_ptr<Connector> connector, bool host)
        : executable_{executable}, port_{port}, connector_{std::move(connector)}, host_{host}
    {
    }
    ~Attempt()
    {
        if (link_ || failed_) {
            hostwin_set_input_mode(HOST_INPUT_LIVE);
        }
    }
    Attempt(const Attempt &) = delete;
    Attempt &operator=(const Attempt &) = delete;
    Attempt(Attempt &&) = delete;
    Attempt &operator=(Attempt &&) = delete;

    [[nodiscard]] OnlineView view() const
    {
        OnlineView view;
        view.host = host_;
        view.problem = problem_;
        if (failed_) {
            view.stage = OnlineView::Stage::Failed;
        } else if (session_) {
            view.stage = OnlineView::Stage::Playing;
        } else if (link_) {
            view.stage = OnlineView::Stage::Syncing;
        } else {
            const Connector::View meeting = connector_->view();
            view.code = meeting.code;
            switch (meeting.step) {
            case Connector::Step::Gathering: view.stage = OnlineView::Stage::Starting; break;
            case Connector::Step::Replying: view.stage = OnlineView::Stage::Replying; break;
            default: view.stage = host_ ? OnlineView::Stage::Inviting : OnlineView::Stage::Joining; break;
            }
        }
        return view;
    }

    // The host's code while nobody has arrived.
    [[nodiscard]] std::string invite() const
    {
        if (!host_ || link_ || failed_) {
            return {};
        }
        const Connector::View meeting = connector_->view();
        return meeting.step == Connector::Step::Inviting ? meeting.code : std::string{};
    }

    // A host still meeting takes a friend's reply.
    [[nodiscard]] bool takes_reply() const noexcept
    {
        return host_ && !link_ && !failed_;
    }
    std::optional<lucent::p2p::CodeProblem> reply(std::string_view code)
    {
        return connector_->reply(code);
    }

    [[nodiscard]] bool holding() const noexcept
    {
        return link_ && !session_ && !failed_;
    }
    [[nodiscard]] bool playing() const noexcept
    {
        return session_ != nullptr;
    }

    // Between frames, while the game runs or holds.
    void poll()
    {
        if (failed_) {
            return;
        }
        if (!link_) {
            meet();
            return;
        }
        if (holding()) {
            hostwin_pump();
        }
        pump(holding() ? 1ms : 0ms);
    }

    // One logic frame of the session, waiting while the peer is too far behind. false on quit.
    bool advance()
    {
        for (;;) {
            if (!pump(0ms)) {
                return end_play();
            }
            hostwin_pump();
            const std::uint8_t local = input_sample_local() & 0x7fU;
            const lucent::rollback::Status status = session_->tick(std::span<const std::uint8_t>{&local, 1});
            link_->send(kRollbackKind, session_->message_for(0));
            if (runtime_->quit()) {
                return false;
            }
            if (status == lucent::rollback::Status::Desynced) {
                lf2_log_writef(LF2_LOG_ERROR, "online", "desync at frame %lld; the session is over",
                               static_cast<long long>(session_->desync_frame().value_or(-1)));
                fail("The two games went out of step");
                return end_play();
            }
            if (status == lucent::rollback::Status::Advanced) {
                report_hashes();
                return true;
            }
            if (!pump(1ms)) {
                return end_play();
            }
        }
    }

  private:
    void meet()
    {
        const Connector::View meeting = connector_->view();
        if (meeting.step == Connector::Step::Failed) {
            fail(connector_problem(meeting.failure));
            return;
        }
        if (!meeting.code.empty() && meeting.code != shown_code_) {
            shown_code_ = meeting.code;
            lf2_log_writef(LF2_LOG_INFO, "online", "%s code: %s",
                           meeting.step == Connector::Step::Replying ? "reply" : "invite", shown_code_.c_str());
        }
        if (meeting.step == Connector::Step::Connected) {
            connect();
        }
    }

    void connect()
    {
        connection_ = connector_->take();
        const Role own = connection_->role;
        const Role other = own == Role::Host ? Role::Friend : Role::Host;
        send_key_ = connection_->keys.key(lucent::p2p::kLinkPurpose, own);
        receive_key_ = connection_->keys.key(lucent::p2p::kLinkPurpose, other);
        link_.emplace(*connection_->path, connection_->peer, lucent::p2p::LinkKeys{send_key_, receive_key_},
                      link_caps());
        const lucent::p2p::crypto::Bytes session = connection_->keys.key(kSessionPurpose, Role::Host);
        std::copy_n(session.begin(), session_id_.size(), session_id_.begin());
        lf2_log_writef(LF2_LOG_INFO, "online", "connected as %s, player slot %d", host_ ? "host" : "friend",
                       host_ ? 0 : 1);
        // From here the guest takes no input of its own, so the state the host offers stays its state.
        if (rmlui_active()) {
            pause_menu_close();
        }
        hostwin_set_input_mode(HOST_INPUT_WITHHELD);
        Lf2NetplayHello hello{};
        std::copy(session_id_.begin(), session_id_.end(), hello.session_id);
        std::copy(executable_.begin(), executable_.end(), hello.executable_sha256);
        std::copy(port_.begin(), port_.end(), hello.port_sha256);
        link_->send(kHelloKind, encode(hello));
    }

    // false once the link is gone.
    bool pump(std::chrono::milliseconds wait)
    {
        link_->pump(wait);
        while (std::optional<lucent::p2p::LinkMessage> message = link_->take()) {
            take(*message);
            if (failed_) {
                return false;
            }
        }
        if (link_->broken() || link_->silent()) {
            lf2_log_write(LF2_LOG_ERROR, "online", "the peer's link broke or went silent; the session is over");
            fail("The other player's connection went quiet");
            return false;
        }
        return true;
    }

    void take(const lucent::p2p::LinkMessage &message)
    {
        if (message.kind == kRollbackKind) {
            if (session_) {
                session_->receive(0, message.body);
            }
            return;
        }
        if (message.kind == kSyncKind) {
            take_sync(message.body);
            return;
        }
        Lf2NetplayHello hello{};
        if (message.kind != kHelloKind ||
            !lf2_netplay_hello_decode(reinterpret_cast<const std::uint8_t *>(message.body.data()), message.body.size(),
                                      &hello)) {
            lf2_log_write(LF2_LOG_ERROR, "online", "the peer sent a malformed message; ignoring it");
            return;
        }
        take_hello(hello);
    }

    void take_hello(const Lf2NetplayHello &hello)
    {
        if (!std::equal(session_id_.begin(), session_id_.end(), hello.session_id)) {
            lf2_log_write(LF2_LOG_ERROR, "online", "the peer's hello names another session; refusing it");
            return;
        }
        if (!std::equal(executable_.begin(), executable_.end(), hello.executable_sha256)) {
            refuse("The other player runs a different lf2.exe");
            return;
        }
        if (!std::equal(port_.begin(), port_.end(), hello.port_sha256)) {
            refuse("The other player runs a different version of this port");
            return;
        }
        lf2_log_write(LF2_LOG_INFO, "online", "the peer runs the same lf2.exe");
        state_ = RollbackState::start();
        if (!state_) {
            refuse("Online play cannot run on this platform");
            return;
        }
        if (host_) {
            offer();
        }
    }

    void offer()
    {
        const std::vector<std::uint8_t> image = state_->host_image();
        const std::vector<RollbackState::PageHash> pages = state_->page_hashes();
        Writer out{SyncStep::Offer};
        out.put(static_cast<std::uint32_t>(image.size()));
        out.put_bytes(image);
        out.put(static_cast<std::uint32_t>(pages.size()));
        for (const RollbackState::PageHash &page : pages) {
            out.put(page.first);
            out.put(page.second);
        }
        link_->send(kSyncKind, out.body());
        lf2_log_writef(LF2_LOG_INFO, "online", "offering this game's state: %zu logic pages", pages.size());
    }

    void take_sync(std::string_view body)
    {
        if (body.empty() || !state_) {
            return;
        }
        Reader in{body.substr(1)};
        switch (static_cast<SyncStep>(body.front())) {
        case SyncStep::Offer: take_offer(in); break;
        case SyncStep::Want: take_want(in); break;
        case SyncStep::Pages: take_pages(in); break;
        case SyncStep::Loaded: take_loaded(in); break;
        case SyncStep::Play: start_play(); break;
        case SyncStep::Refuse: fail(std::string{body.substr(1)}); break;
        default: lf2_log_write(LF2_LOG_ERROR, "online", "the peer sent an unknown sync step; ignoring it"); break;
        }
    }

    void take_offer(Reader &in)
    {
        std::uint32_t image_size = 0;
        std::uint32_t page_count = 0;
        std::optional<std::span<const std::uint8_t>> image;
        if (host_ || !in.get(image_size) || !(image = in.get_bytes(image_size)) || !in.get(page_count)) {
            refuse("The host's state did not read");
            return;
        }
        std::string why;
        if (!state_->adopt(*image, why)) {
            refuse(why);
            return;
        }
        const std::vector<RollbackState::PageHash> own = state_->page_hashes();
        std::vector<std::uint32_t> wanted;
        for (std::uint32_t i = 0; i < page_count; ++i) {
            RollbackState::PageHash page{};
            if (!in.get(page.first) || !in.get(page.second)) {
                refuse("The host's state did not read");
                return;
            }
            const auto found = std::lower_bound(own.begin(), own.end(), page,
                                                [](const auto &a, const auto &b) { return a.first < b.first; });
            if (found == own.end() || found->first != page.first) {
                refuse("The host's game has memory this one has not");
                return;
            }
            if (found->second != page.second) {
                wanted.push_back(page.first);
            }
        }
        pages_wanted_ = wanted.size();
        Writer out{SyncStep::Want};
        out.put(static_cast<std::uint32_t>(wanted.size()));
        for (const std::uint32_t address : wanted) {
            out.put(address);
        }
        link_->send(kSyncKind, out.body());
        lf2_log_writef(LF2_LOG_INFO, "online", "taking the host's state: %zu of %u pages differ", wanted.size(),
                       page_count);
        if (wanted.empty()) {
            send_loaded();
        }
    }

    void take_want(Reader &in)
    {
        std::uint32_t count = 0;
        if (!host_ || !in.get(count)) {
            refuse("The friend's request did not read");
            return;
        }
        std::vector<std::uint32_t> wanted(count);
        for (std::uint32_t &address : wanted) {
            if (!in.get(address)) {
                refuse("The friend's request did not read");
                return;
            }
        }
        for (std::size_t first = 0; first < wanted.size(); first += kPagesPerMessage) {
            const std::size_t last = std::min(wanted.size(), first + kPagesPerMessage);
            Writer out{SyncStep::Pages};
            out.put(static_cast<std::uint32_t>(last - first));
            for (std::size_t i = first; i < last; ++i) {
                out.put(wanted[i]);
                out.put_bytes(RollbackState::page(wanted[i]));
            }
            link_->send(kSyncKind, out.body());
        }
    }

    void take_pages(Reader &in)
    {
        std::uint32_t count = 0;
        if (host_ || !in.get(count)) {
            refuse("The host's pages did not read");
            return;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t address = 0;
            std::optional<std::span<const std::uint8_t>> bytes;
            if (!in.get(address) || !(bytes = in.get_bytes(kStatePageSize))) {
                refuse("The host's pages did not read");
                return;
            }
            state_->write_page(address, *bytes);
        }
        pages_taken_ += count;
        if (pages_taken_ >= pages_wanted_) {
            send_loaded();
        }
    }

    void send_loaded()
    {
        Writer out{SyncStep::Loaded};
        out.put(state_->hash());
        link_->send(kSyncKind, out.body());
    }

    void take_loaded(Reader &in)
    {
        std::uint64_t theirs = 0;
        if (!host_ || !in.get(theirs)) {
            refuse("The friend's state did not read");
            return;
        }
        const std::uint64_t ours = state_->hash();
        if (theirs != ours) {
            lf2_log_writef(LF2_LOG_ERROR, "online", "the friend's state hash %016llx is not ours, %016llx",
                           static_cast<unsigned long long>(theirs), static_cast<unsigned long long>(ours));
            refuse("The friend's game did not take this one's state");
            return;
        }
        link_->send(kSyncKind, Writer{SyncStep::Play}.body());
        start_play();
    }

    void start_play()
    {
        runtime_ = std::make_unique<GameRuntime>(std::move(state_));
        lucent::rollback::Config config;
        config.slots = 2;
        config.input_bytes = 1;
        config.local_slots = {host_ ? 0 : 1};
        config.max_rollback = RollbackState::kSlots - 1;
        session_ = std::make_unique<lucent::rollback::Session>(config, *runtime_, 1);
        lf2_log_write(LF2_LOG_INFO, "online", "playing through the rollback engine");
    }

    // The session is over; the game goes on alone from where it is.
    bool end_play()
    {
        session_.reset();
        runtime_.reset();
        hostwin_set_input_mode(HOST_INPUT_LIVE);
        return main_loop_advance() != 0;
    }

    void refuse(const std::string &why)
    {
        if (link_ && !failed_) {
            Writer out{SyncStep::Refuse};
            out.put_bytes({reinterpret_cast<const std::uint8_t *>(why.data()), why.size()});
            link_->send(kSyncKind, out.body());
            link_->pump(0ms);
        }
        fail(why);
    }

    void fail(const std::string &why)
    {
        if (failed_) {
            return;
        }
        failed_ = true;
        problem_ = why;
        lf2_log_writef(LF2_LOG_ERROR, "online", "the session ended: %s", why.c_str());
        if (!session_) {
            state_.reset();
            hostwin_set_input_mode(HOST_INPUT_LIVE);
        }
    }

    void report_hashes()
    {
        while (next_report_ < session_->finalized_frame()) {
            lf2_log_writef(LF2_LOG_INFO, "online", "frame %lld final hash %016llx, %lld frames resimulated",
                           static_cast<long long>(next_report_),
                           static_cast<unsigned long long>(session_->final_hash(next_report_).value()),
                           static_cast<long long>(session_->resimulated_frames()));
            next_report_ += kHashReportFrames;
        }
    }

    lucent::content::Sha256 executable_;
    lucent::content::Sha256 port_;
    std::unique_ptr<Connector> connector_;
    bool host_ = false;
    std::unique_ptr<lucent::p2p::Connection> connection_;
    lucent::p2p::crypto::Bytes send_key_;
    lucent::p2p::crypto::Bytes receive_key_;
    std::optional<lucent::p2p::ReliableLink> link_;
    std::array<std::uint8_t, 16> session_id_{};
    std::string shown_code_;
    std::unique_ptr<RollbackState> state_;
    std::size_t pages_wanted_ = 0;
    std::size_t pages_taken_ = 0;
    std::unique_ptr<GameRuntime> runtime_;
    std::unique_ptr<lucent::rollback::Session> session_;
    std::int64_t next_report_ = kHashReportFrames;
    bool failed_ = false;
    std::string problem_;
};

// Everything online this process holds: what identifies its game, its ZeroTier node, the open
// games and one attempt at a time.
class Online {
  public:
    Online() : listed_(opt_online_listed() != 0), lobbies_(directories())
    {
    }

    [[nodiscard]] OnlineView view() const
    {
        OnlineView view = attempt_ ? attempt_->view() : OnlineView{};
        if (asking_) {
            view.stage = OnlineView::Stage::Asking;
        }
        if (!problem_.empty()) {
            view.problem = problem_;
        }
        view.listed = listed_ && attempt_ && !attempt_->invite().empty();
        return view;
    }

    void host()
    {
        if (!ready()) {
            return;
        }
        asking_ = false;
        attempt_.reset();
        lobby_id_ =
            hex(std::span<const std::uint8_t>{port_.data(), 4}) + "-" + hex(lucent::p2p::crypto::random_bytes(4));
        attempt_ = std::make_unique<Attempt>(executable_, port_, std::make_unique<Connector>(connector_config()), true);
        lf2_log_write(LF2_LOG_INFO, "online", "hosting: waiting for the invite code");
    }

    std::string paste(std::string_view code)
    {
        if (attempt_ && attempt_->takes_reply()) {
            const std::optional<lucent::p2p::CodeProblem> problem = attempt_->reply(code);
            return problem ? code_problem(*problem) : std::string{};
        }
        lucent::p2p::CodeProblem problem{};
        std::optional<lucent::p2p::Invite> invite = lucent::p2p::read_invite(code, kPrefixes, problem);
        if (!invite) {
            return code_problem(problem);
        }
        if (!ready()) {
            return problem_;
        }
        asking_ = false;
        attempt_.reset();
        attempt_ = std::make_unique<Attempt>(
            executable_, port_, std::make_unique<Connector>(connector_config(), std::move(*invite)), false);
        lf2_log_write(LF2_LOG_INFO, "online", "joining the invite");
        return {};
    }

    void join(const lucent::p2p::Lobby &lobby)
    {
        if (!ready()) {
            return;
        }
        attempt_.reset();
        lobbies_.offer(std::nullopt);
        asking_ = true;
        lobbies_.ask(lobby);
    }

    void leave()
    {
        asking_ = false;
        attempt_.reset();
        lobbies_.offer(std::nullopt);
        input_use_session(nullptr, 0);
    }

    void set_listed(bool listed)
    {
        listed_ = listed;
        opt_set_online_listed(listed ? 1 : 0);
    }

    [[nodiscard]] std::vector<lucent::p2p::Lobby> lobbies() const
    {
        std::vector<lucent::p2p::Lobby> open = lobbies_.lobbies();
        std::erase_if(open, [this](const lucent::p2p::Lobby &lobby) { return lobby.version != version_; });
        return open;
    }

    void refresh_lobbies()
    {
        if (ready()) {
            lobbies_.refresh();
        }
    }

    void poll()
    {
        if (asking_) {
            take_seat();
        }
        if (!attempt_) {
            return;
        }
        attempt_->poll();
        const std::string invite = attempt_->invite();
        if (!listed_ || invite.empty()) {
            lobbies_.offer(std::nullopt);
            return;
        }
        lucent::p2p::Lobby lobby;
        lobby.id = lobby_id_;
        lobby.title = "Little Fighter 2";
        lobby.players = 1;
        lobby.capacity = 2;
        lobby.version = version_;
        lobbies_.offer(LobbyList::Offer{std::move(lobby), invite});
    }

    [[nodiscard]] bool holding() const
    {
        return attempt_ && attempt_->holding();
    }
    [[nodiscard]] bool playing() const
    {
        return attempt_ && attempt_->playing();
    }
    bool advance()
    {
        return attempt_->advance();
    }

  private:
    void take_seat()
    {
        const std::optional<LobbyList::SeatAnswer> answer = lobbies_.seat();
        if (!answer || answer->seat == LobbyList::Seat::Waiting) {
            return;
        }
        asking_ = false;
        switch (answer->seat) {
        case LobbyList::Seat::Given:
            if (std::string problem = paste(answer->invite); !problem.empty()) {
                problem_ = std::move(problem);
            }
            return;
        case LobbyList::Seat::Full: problem_ = "That game is full"; return;
        case LobbyList::Seat::Unanswered: problem_ = "That game's host did not answer"; return;
        case LobbyList::Seat::Failed:
        case LobbyList::Seat::Waiting: problem_ = "Could not reach that game's list"; return;
        }
    }

    // What the hello compares and the ZeroTier node, made the first time they are needed.
    bool ready()
    {
        problem_.clear();
        if (identified_) {
            return true;
        }
        std::string error;
        const std::optional<lucent::content::Sha256> executable = lucent::content::sha256_file("lf2.exe", error);
        const std::optional<std::filesystem::path> self = lucent::platform::executable_path();
        std::optional<lucent::content::Sha256> port;
        if (self) {
            port = lucent::content::sha256_file(*self, error);
        }
        if (!executable || !port) {
            lf2_log_writef(LF2_LOG_ERROR, "online", "cannot identify this game for a session: %s", error.c_str());
            problem_ = "This game cannot identify itself";
            return false;
        }
        executable_ = *executable;
        port_ = *port;
        version_ = hex(std::span<const std::uint8_t>{port_.data(), 8});
        zerotier_ = start_zerotier();
        identified_ = true;
        return true;
    }

    [[nodiscard]] lucent::p2p::ConnectorConfig connector_config() const
    {
        lucent::p2p::ConnectorConfig config;
        config.prefixes = kPrefixes;
        config.label = std::string{kKeyLabel};
        config.zerotier = zerotier_.get();
        if (lf2_environment_get(LF2_ENV_ONLINE_ZEROTIER_ONLY)) {
            config.direct.stun_servers = "";
            config.direct.router = false;
            config.direct.interfaces = std::vector<lucent::net::Ipv4Interface>{};
        }
        return config;
    }

    bool identified_ = false;
    lucent::content::Sha256 executable_{};
    lucent::content::Sha256 port_{};
    std::string version_;
    std::string problem_;
    bool listed_;
    bool asking_ = false;
    std::string lobby_id_;
    std::unique_ptr<lucent::p2p::ZeroTierNode> zerotier_;
    LobbyList lobbies_;
    std::unique_ptr<Attempt> attempt_;
};

std::unique_ptr<Online> online;

Online &the_online()
{
    if (!online) {
        online = std::make_unique<Online>();
    }
    return *online;
}

} // namespace

OnlineView online_view()
{
    return the_online().view();
}

void online_host()
{
    the_online().host();
}

std::string online_paste(std::string_view code)
{
    return the_online().paste(code);
}

void online_leave()
{
    the_online().leave();
}

void online_set_listed(bool listed)
{
    the_online().set_listed(listed);
}

std::vector<lucent::p2p::Lobby> online_lobbies()
{
    return the_online().lobbies();
}

void online_join(const lucent::p2p::Lobby &lobby)
{
    the_online().join(lobby);
}

void online_refresh_lobbies()
{
    the_online().refresh_lobbies();
}

} // namespace lf2::netplay

extern "C" void lf2_online_poll(void)
{
    if (lf2::netplay::online) {
        lf2::netplay::online->poll();
    }
}

extern "C" int lf2_online_holding(void)
{
    return lf2::netplay::online && lf2::netplay::online->holding() ? 1 : 0;
}

extern "C" int lf2_online_playing(void)
{
    return lf2::netplay::online && lf2::netplay::online->playing() ? 1 : 0;
}

extern "C" int lf2_online_advance(void)
{
    return lf2::netplay::online->advance() ? 1 : 0;
}

extern "C" void lf2_online_shutdown(void)
{
    lf2::netplay::online.reset();
}
