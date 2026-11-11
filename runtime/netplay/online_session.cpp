#include "online_session.h"

#include "environment.h"
#include "lf2_log.h"
#include "packet.h"

#include <lucent/content.h>
#include <lucent/p2p/connector.h>
#include <lucent/p2p/reliable_link.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lf2::netplay {
namespace {

using namespace std::chrono_literals;
using lucent::p2p::Connector;
using lucent::p2p::Role;

constexpr lucent::p2p::CodePrefixes kPrefixes{"LF2", "LFR"};
constexpr std::string_view kKeyLabel = "lf2-online";
constexpr std::string_view kSessionPurpose = "lf2-session";
constexpr std::uint8_t kHelloKind = LF2_NETPLAY_HELLO;
constexpr std::uint8_t kInputKind = LF2_NETPLAY_INPUT;

lucent::p2p::LinkCaps link_caps()
{
    lucent::p2p::LinkCaps caps{};
    caps[kHelloKind] = LF2_NETPLAY_HELLO_BYTES;
    caps[kInputKind] = LF2_NETPLAY_INPUT_BYTES;
    return caps;
}

std::string encode(const Lf2NetplayPacket &packet)
{
    std::array<std::uint8_t, LF2_NETPLAY_HELLO_BYTES> bytes{};
    const std::size_t size = lf2_netplay_packet_encode(&packet, bytes.data(), bytes.size());
    return {reinterpret_cast<const char *>(bytes.data()), size};
}

class OnlineSession {
  public:
    OnlineSession(lucent::content::Sha256 executable, std::unique_ptr<Connector> connector)
        : executable_{executable}, connector_{std::move(connector)}
    {
    }

    void frame(std::uint32_t frame, std::uint8_t local_buttons)
    {
        if (!link_) {
            meet();
            return;
        }
        link_->pump(0ms);
        while (std::optional<lucent::p2p::LinkMessage> message = link_->take()) {
            take(*message);
        }
        if (link_->broken() || link_->silent()) {
            lf2_log_write(LF2_LOG_ERROR, "online", "the peer's link broke or went silent; the session is over");
            link_.reset();
            connection_.reset();
            return;
        }
        if (peer_verified_ && !input_sent_) {
            send_input(frame, local_buttons);
        }
    }

  private:
    void meet()
    {
        const Connector::View view = connector_->view();
        if (view.step == Connector::Step::Failed) {
            if (!reported_failure_) {
                lf2_log_writef(LF2_LOG_ERROR, "online", "the meeting failed (reason %d)",
                               static_cast<int>(view.failure));
                reported_failure_ = true;
            }
            return;
        }
        if (!view.code.empty() && view.code != shown_code_) {
            shown_code_ = view.code;
            lf2_log_writef(LF2_LOG_INFO, "online", "%s code: %s",
                           view.step == Connector::Step::Replying ? "reply" : "invite", shown_code_.c_str());
        }
        if (view.step != Connector::Step::Connected) {
            return;
        }
        connection_ = connector_->take();
        const Role own = connection_->role;
        const Role other = own == Role::Host ? Role::Friend : Role::Host;
        send_key_ = connection_->keys.key(lucent::p2p::kLinkPurpose, own);
        receive_key_ = connection_->keys.key(lucent::p2p::kLinkPurpose, other);
        link_.emplace(*connection_->path, connection_->peer, lucent::p2p::LinkKeys{send_key_, receive_key_},
                      link_caps());
        const lucent::p2p::crypto::Bytes session = connection_->keys.key(kSessionPurpose, Role::Host);
        std::copy_n(session.begin(), session_id_.size(), session_id_.begin());
        slot_ = own == Role::Host ? 0 : 1;
        lf2_log_writef(LF2_LOG_INFO, "online", "connected as %s, player slot %u", own == Role::Host ? "host" : "friend",
                       slot_);

        Lf2NetplayPacket hello{};
        hello.type = LF2_NETPLAY_HELLO;
        std::copy(session_id_.begin(), session_id_.end(), hello.data.hello.session_id);
        std::copy(executable_.begin(), executable_.end(), hello.data.hello.executable_sha256);
        link_->send(kHelloKind, encode(hello));
    }

    void take(const lucent::p2p::LinkMessage &message)
    {
        Lf2NetplayPacket packet{};
        if (!lf2_netplay_packet_decode(reinterpret_cast<const std::uint8_t *>(message.body.data()), message.body.size(),
                                       &packet) ||
            packet.type != message.kind) {
            lf2_log_write(LF2_LOG_ERROR, "online", "the peer sent a malformed packet; ignoring it");
            return;
        }
        if (packet.type == LF2_NETPLAY_HELLO) {
            take_hello(packet);
        } else if (peer_verified_) {
            lf2_log_writef(LF2_LOG_INFO, "online", "frame %u received: slot %u buttons 0x%02x", packet.data.input.frame,
                           packet.data.input.player_slot, packet.data.input.buttons);
        }
    }

    void take_hello(const Lf2NetplayPacket &packet)
    {
        if (!std::equal(session_id_.begin(), session_id_.end(), packet.data.hello.session_id)) {
            lf2_log_write(LF2_LOG_ERROR, "online", "the peer's hello names another session; refusing it");
            return;
        }
        if (!std::equal(executable_.begin(), executable_.end(), packet.data.hello.executable_sha256)) {
            lf2_log_write(LF2_LOG_ERROR, "online", "the peer runs a different lf2.exe; refusing the session");
            return;
        }
        peer_verified_ = true;
        lf2_log_write(LF2_LOG_INFO, "online", "the peer runs the same lf2.exe");
    }

    void send_input(std::uint32_t frame, std::uint8_t local_buttons)
    {
        Lf2NetplayPacket input{};
        input.type = LF2_NETPLAY_INPUT;
        std::copy(session_id_.begin(), session_id_.end(), input.data.input.session_id);
        input.data.input.frame = frame;
        input.data.input.player_slot = slot_;
        input.data.input.buttons = local_buttons & LF2_NETPLAY_BUTTON_MASK;
        link_->send(kInputKind, encode(input));
        input_sent_ = true;
        lf2_log_writef(LF2_LOG_INFO, "online", "frame %u sent: slot %u buttons 0x%02x", frame, slot_,
                       input.data.input.buttons);
    }

    lucent::content::Sha256 executable_;
    std::unique_ptr<Connector> connector_;
    std::unique_ptr<lucent::p2p::Connection> connection_;
    lucent::p2p::crypto::Bytes send_key_;
    lucent::p2p::crypto::Bytes receive_key_;
    std::optional<lucent::p2p::ReliableLink> link_;
    std::array<std::uint8_t, 16> session_id_{};
    std::string shown_code_;
    std::uint8_t slot_ = 0;
    bool peer_verified_ = false;
    bool input_sent_ = false;
    bool reported_failure_ = false;
};

std::unique_ptr<OnlineSession> session;

lucent::p2p::ConnectorConfig connector_config()
{
    lucent::p2p::ConnectorConfig config;
    config.prefixes = kPrefixes;
    config.label = std::string{kKeyLabel};
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
}

extern "C" void lf2_online_frame(uint32_t frame, uint8_t local_buttons)
{
    if (lf2::netplay::session) {
        lf2::netplay::session->frame(frame, local_buttons);
    }
}
