#include "online_session.h"

#include "environment.h"
#include "lf2_log.h"
#include "game_runtime.h"
#include "guest_clock.h"
#include "hostwin.h"
#include "main_loop.h"
#include "packet.h"
#include "session_inputs.h"
#include "user_paths.h"

#include <lucent/content.h>
#include <lucent/p2p/connector.h>
#include <lucent/p2p/reliable_link.h>
#include <lucent/p2p/zerotier.h>
#include <lucent/rollback/session.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lf2::netplay {
namespace {

using namespace std::chrono_literals;
using lucent::p2p::Connector;
using lucent::p2p::Role;

constexpr lucent::p2p::CodePrefixes kPrefixes{"LF2", "LFR"};
constexpr std::string_view kKeyLabel = "lf2-online";
constexpr std::string_view kSessionPurpose = "lf2-session";
// LF2's port, which names its ZeroTier ad-hoc network.
constexpr std::uint16_t kZeroTierPort = 41702;
constexpr std::uint8_t kHelloKind = 1;
constexpr std::uint8_t kRollbackKind = 3;
constexpr std::size_t kRollbackMessageBytes = 4096;
// A final hash is logged this often, for a check that both peers played the same frames.
constexpr std::int64_t kHashReportFrames = 60;

lucent::p2p::LinkCaps link_caps()
{
    lucent::p2p::LinkCaps caps{};
    caps[kHelloKind] = LF2_NETPLAY_HELLO_BYTES;
    caps[kRollbackKind] = kRollbackMessageBytes;
    return caps;
}

std::string encode(const Lf2NetplayHello &hello)
{
    std::array<std::uint8_t, LF2_NETPLAY_HELLO_BYTES> bytes{};
    const std::size_t size = lf2_netplay_hello_encode(&hello, bytes.data(), bytes.size());
    return {reinterpret_cast<const char *>(bytes.data()), size};
}

// Two peers meet before either guest boots, agree on the session, the executable and the host's
// calendar, and then play every frame through the rollback engine.
class OnlineSession {
  public:
    OnlineSession(lucent::content::Sha256 executable, std::unique_ptr<Connector> connector)
        : executable_{executable}, connector_{std::move(connector)}
    {
    }

    // Blocks until the peers agree or the meeting fails; true when they play.
    bool meet()
    {
        while (!link_) {
            const Connector::View view = connector_->view();
            if (view.step == Connector::Step::Failed) {
                lf2_log_writef(LF2_LOG_ERROR, "online", "the meeting failed (reason %d)",
                               static_cast<int>(view.failure));
                return false;
            }
            if (!view.code.empty() && view.code != shown_code_) {
                shown_code_ = view.code;
                lf2_log_writef(LF2_LOG_INFO, "online", "%s code: %s",
                               view.step == Connector::Step::Replying ? "reply" : "invite", shown_code_.c_str());
            }
            if (view.step == Connector::Step::Connected) {
                connect();
            } else {
                std::this_thread::sleep_for(10ms);
            }
        }
        while (!peer_verified_) {
            if (!pump(10ms)) {
                return false;
            }
        }
        return start_play();
    }

    [[nodiscard]] bool playing() const noexcept
    {
        return session_ != nullptr;
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
        host_ = own == Role::Host;
        lf2_log_writef(LF2_LOG_INFO, "online", "connected as %s, player slot %d", host_ ? "host" : "friend",
                       host_ ? 0 : 1);

        Lf2NetplayHello hello{};
        std::copy(session_id_.begin(), session_id_.end(), hello.session_id);
        std::copy(executable_.begin(), executable_.end(), hello.executable_sha256);
        if (host_) {
            const GuestCalendar calendar = guest_clock_calendar();
            hello.epoch_ns = calendar.epoch_ns;
            hello.utc_offset_s = calendar.utc_offset_s;
        }
        link_->send(kHelloKind, encode(hello));
    }

    // false once the link is gone.
    bool pump(std::chrono::milliseconds wait)
    {
        link_->pump(wait);
        while (std::optional<lucent::p2p::LinkMessage> message = link_->take()) {
            take(*message);
        }
        if (link_->broken() || link_->silent()) {
            lf2_log_write(LF2_LOG_ERROR, "online", "the peer's link broke or went silent; the session is over");
            return false;
        }
        return true;
    }

    void take(const lucent::p2p::LinkMessage &message)
    {
        if (message.kind == kRollbackKind && session_) {
            session_->receive(0, message.body);
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
            lf2_log_write(LF2_LOG_ERROR, "online", "the peer runs a different lf2.exe; refusing the session");
            return;
        }
        if (!host_ && !guest_clock_adopt_calendar(GuestCalendar{hello.epoch_ns, hello.utc_offset_s})) {
            return;
        }
        peer_verified_ = true;
        lf2_log_write(LF2_LOG_INFO, "online", "the peer runs the same lf2.exe");
    }

    bool start_play()
    {
        std::unique_ptr<RollbackState> state = RollbackState::start();
        if (!state) {
            lf2_log_write(LF2_LOG_ERROR, "online", "no rollback state on this platform; playing offline");
            return false;
        }
        runtime_ = std::make_unique<GameRuntime>(std::move(state));
        lucent::rollback::Config config;
        config.slots = 2;
        config.input_bytes = 1;
        config.local_slots = {host_ ? 0 : 1};
        config.max_rollback = RollbackState::kSlots - 1;
        session_ = std::make_unique<lucent::rollback::Session>(config, *runtime_, 1);
        lf2_log_write(LF2_LOG_INFO, "online", "playing through the rollback engine");
        return true;
    }

    bool end_play()
    {
        session_.reset();
        runtime_.reset();
        return main_loop_advance() != 0;
    }

    void report_hashes()
    {
        while (const std::optional<std::uint64_t> hash = session_->final_hash(next_report_)) {
            lf2_log_writef(LF2_LOG_INFO, "online", "frame %lld final hash %016llx, %lld frames resimulated",
                           static_cast<long long>(next_report_), static_cast<unsigned long long>(*hash),
                           static_cast<long long>(session_->resimulated_frames()));
            next_report_ += kHashReportFrames;
        }
    }

    lucent::content::Sha256 executable_;
    std::unique_ptr<Connector> connector_;
    std::unique_ptr<lucent::p2p::Connection> connection_;
    lucent::p2p::crypto::Bytes send_key_;
    lucent::p2p::crypto::Bytes receive_key_;
    std::optional<lucent::p2p::ReliableLink> link_;
    std::array<std::uint8_t, 16> session_id_{};
    std::string shown_code_;
    bool host_ = false;
    bool peer_verified_ = false;
    std::unique_ptr<GameRuntime> runtime_;
    std::unique_ptr<lucent::rollback::Session> session_;
    std::int64_t next_report_ = kHashReportFrames;
};

// The process's one ZeroTier node, which outlives the session that dials through it.
std::unique_ptr<lucent::p2p::ZeroTierNode> zerotier_node;
std::unique_ptr<OnlineSession> session;

std::optional<std::string> read_file(const char *path)
{
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, {}};
}

void start_zerotier()
{
    char storage[1024];
    char error[256];
    if (!user_paths_zerotier_directory(storage, sizeof storage, error, sizeof error)) {
        lf2_log_writef(LF2_LOG_WARNING, "online", "no ZeroTier: %s; meeting direct only", error);
        return;
    }
    lucent::p2p::ZeroTierConfig config{.storage = storage, .roots = {}, .port = 0, .network_port = kZeroTierPort};
    if (const char *roots = lf2_environment_get(LF2_ENV_ZEROTIER_ROOTS)) {
        std::optional<std::string> set = read_file(roots);
        if (!set) {
            lf2_log_writef(LF2_LOG_WARNING, "online", "cannot read the ZeroTier root set %s; meeting direct only",
                           roots);
            return;
        }
        config.roots = std::move(*set);
    }
    std::string why;
    zerotier_node = lucent::p2p::ZeroTierNode::start(config, why);
    if (!zerotier_node) {
        lf2_log_writef(LF2_LOG_WARNING, "online", "no ZeroTier: %s; meeting direct only", why.c_str());
    }
}

lucent::p2p::ConnectorConfig connector_config()
{
    lucent::p2p::ConnectorConfig config;
    config.prefixes = kPrefixes;
    config.label = std::string{kKeyLabel};
    config.zerotier = zerotier_node.get();
    if (lf2_environment_get(LF2_ENV_ONLINE_ZEROTIER_ONLY)) {
        config.direct.stun_servers = "";
        config.direct.router = false;
        config.direct.interfaces = std::vector<lucent::net::Ipv4Interface>{};
    }
    return config;
}

} // namespace
} // namespace lf2::netplay

extern "C" void lf2_online_start(void)
{
    using namespace lf2::netplay;
    const char *mode = lf2_environment_get(LF2_ENV_ONLINE);
    if (!mode || !*mode) {
        return;
    }
    std::string error;
    const std::optional<lucent::content::Sha256> executable = lucent::content::sha256_file("lf2.exe", error);
    if (!executable) {
        lf2_log_writef(LF2_LOG_ERROR, "online", "cannot hash lf2.exe for the session hello: %s", error.c_str());
        return;
    }
    start_zerotier();
    std::unique_ptr<lucent::p2p::Connector> connector;
    if (std::string_view{mode} == "host") {
        connector = std::make_unique<lucent::p2p::Connector>(connector_config());
        lf2_log_write(LF2_LOG_INFO, "online", "hosting: waiting for the invite code");
    } else {
        lucent::p2p::CodeProblem problem{};
        std::optional<lucent::p2p::Invite> invite = lucent::p2p::read_invite(mode, kPrefixes, problem);
        if (!invite) {
            lf2_log_writef(LF2_LOG_ERROR, "online", "LF2_ONLINE is not an LF2 invite (problem %d)",
                           static_cast<int>(problem));
            return;
        }
        connector = std::make_unique<lucent::p2p::Connector>(connector_config(), std::move(*invite));
        lf2_log_write(LF2_LOG_INFO, "online", "joining the invite");
    }
    session = std::make_unique<OnlineSession>(*executable, std::move(connector));
    if (!session->meet()) {
        session.reset();
    }
}

extern "C" int lf2_online_playing(void)
{
    return lf2::netplay::session && lf2::netplay::session->playing() ? 1 : 0;
}

extern "C" int lf2_online_advance(void)
{
    return lf2::netplay::session->advance() ? 1 : 0;
}
