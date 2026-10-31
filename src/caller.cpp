#include "caller.hpp"
#include "platform.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <io.h>
#include <iostream>
#include <memory>
#include <pj/log.h>
#include <pjsua2.hpp>

namespace aiphone {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

// PJSUA emits startup messages before its configured logger takes effect.
// Preserve the real stdout descriptor until all native objects have shut down.
class NativeOutput {
  public:
    NativeOutput() {
        std::fflush(stdout);
        saved_ = _dup(_fileno(stdout));
        if (saved_ < 0 || _dup2(_fileno(stderr), _fileno(stdout)) != 0) {
            if (saved_ >= 0) {
                _close(saved_);
            }
            throw std::runtime_error("Cannot isolate native diagnostics from JSON output.");
        }
    }
    ~NativeOutput() {
        std::fflush(stdout);
        _dup2(saved_, _fileno(stdout));
        _close(saved_);
    }
    NativeOutput(const NativeOutput &) = delete;
    NativeOutput &operator=(const NativeOutput &) = delete;

  private:
    int saved_ = -1;
};

class Endpoint final : public pj::Endpoint {
  public:
    std::atomic<int> source{-1};
    std::atomic<int> sink{-1};
    std::atomic<bool> connected{false};
    std::atomic<bool> stopped{false};
    std::atomic<int> operation_error{0};

    void onAudioMediaOpCompleted(pj::OnAudioMediaOpCompletedParam &event) override {
        if (event.opType == PJMEDIA_CONF_OP_CONNECT_PORTS &&
            static_cast<int>(event.opParam.connectInfo.mediaId) == source.load() &&
            static_cast<int>(event.opParam.connectInfo.targetMediaId) == sink.load()) {
            if (event.status == PJ_SUCCESS) {
                connected.store(true);
            } else {
                operation_error.store(event.status);
            }
        }
        if (event.opType == PJMEDIA_CONF_OP_DISCONNECT_PORTS &&
            static_cast<int>(event.opParam.disconnectInfo.mediaId) == source.load() &&
            static_cast<int>(event.opParam.disconnectInfo.targetMediaId) == sink.load()) {
            stopped.store(true);
        }
    }
};

class Player final : public pj::AudioMediaPlayer {
  public:
    std::atomic<bool> eof{false};
    void onEof2() override {
        eof.store(true);
    }
};

bool phone_socket(const std::string &address, const std::string &phone) {
    const auto prefix = phone + ":";
    if (address.rfind(prefix, 0) != 0) {
        return false;
    }
    const auto port = address.substr(prefix.size());
    if (port.empty() || port.size() > 5 ||
        port.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    const auto number = std::stoul(port);
    return number > 0 && number <= 65535;
}

class OutgoingCall final : public pj::Call {
  public:
    explicit OutgoingCall(pj::Account &account, const Config &config)
        : pj::Call(account), config_(config) {}

    bool answered = false;
    bool disconnected = false;
    bool media_ready = false;
    bool failed = false;
    int status = 0;
    int media_index = -1;
    Clock::time_point answered_at{};

    void onCallState(pj::OnCallStateParam &) override {
        try {
            const auto info = getInfo();
            if (info.state == PJSIP_INV_STATE_CONFIRMED && !answered) {
                answered = true;
                answered_at = Clock::now();
            }
            disconnected = info.state == PJSIP_INV_STATE_DISCONNECTED;
            if (info.lastStatusCode >= 200) {
                status = info.lastStatusCode;
            }
        } catch (...) {
            failed = true;
        }
    }

    void onCallMediaState(pj::OnCallMediaStateParam &) override {
        try {
            inspect_media();
        } catch (...) {
            failed = true;
            media_ready = false;
        }
    }

    void onCallTransferRequest(pj::OnCallTransferRequestParam &event) override {
        event.statusCode = PJSIP_SC_FORBIDDEN;
    }
    void onCallReplaceRequest(pj::OnCallReplaceRequestParam &event) override {
        event.statusCode = PJSIP_SC_FORBIDDEN;
    }
    pjsip_redirect_op onCallRedirected(pj::OnCallRedirectedParam &) override {
        return PJSIP_REDIRECT_STOP;
    }

    void onCallRxOffer(pj::OnCallRxOfferParam &event) override {
        const auto *sdp = static_cast<const pjmedia_sdp_session *>(event.offer.pjSdpSession);
        bool valid = sdp != nullptr;
        if (sdp) {
            for (unsigned index = 0; index < sdp->media_count; ++index) {
                const auto *media = sdp->media[index];
                if (pj_strcmp2(&media->desc.media, "audio") != 0 || media->desc.port == 0) {
                    valid = false;
                    break;
                }
                const auto *connection = media->conn ? media->conn : sdp->conn;
                if (!connection || pj_strcmp2(&connection->addr, config_.phone_ip.c_str()) != 0 ||
                    pjmedia_sdp_attr_find2(media->attr_count, media->attr, "inactive", nullptr) ||
                    pjmedia_sdp_attr_find2(media->attr_count, media->attr, "sendonly", nullptr)) {
                    valid = false;
                    break;
                }
            }
        }
        if (!valid) {
            event.statusCode = PJSIP_SC_NOT_ACCEPTABLE_HERE;
            failed = true;
        }
        event.opt.audioCount = 1;
        event.opt.videoCount = 0;
        event.opt.textCount = 0;
    }

    void inspect_media() {
        media_ready = false;
        const auto info = getInfo();
        for (const auto &media : info.media) {
            if (media.type != PJMEDIA_TYPE_AUDIO || media.status != PJSUA_CALL_MEDIA_ACTIVE) {
                continue;
            }
            if (!(media.dir & PJMEDIA_DIR_ENCODING)) {
                continue;
            }
            const auto stream = getStreamInfo(media.index);
            const auto transport = getMedTransportInfo(media.index);
            if ((stream.codecName != "PCMU" && stream.codecName != "PCMA") ||
                stream.codecClockRate != 8000 ||
                !phone_socket(stream.remoteRtpAddress, config_.phone_ip) ||
                (!stream.remoteRtcpAddress.empty() &&
                 !phone_socket(stream.remoteRtcpAddress, config_.phone_ip)) ||
                transport.localRtpName !=
                    config_.local_ip + ":" + std::to_string(config_.rtp_port) ||
                transport.localRtcpName !=
                    config_.local_ip + ":" + std::to_string(config_.rtcp_port)) {
                failed = true;
                return;
            }
            media_ready = true;
            media_index = static_cast<int>(media.index);
        }
    }

  private:
    const Config &config_;
};

class RejectingAccount final : public pj::Account {
  public:
    void onIncomingCall(pj::OnIncomingCallParam &event) override {
        pjsua_call_answer(event.callId, PJSIP_SC_BUSY_HERE, nullptr, nullptr);
    }
};

class Session {
  public:
    Session(const Config &config, Result &result) : config_(config), result_(result) {}
    ~Session() {
        cleanup();
    }

    void start(const std::string &caller) {
        pj_log_set_level(0);
        endpoint_.libCreate();
        created_ = true;
        pj::EpConfig options;
        options.uaConfig.threadCnt = 0;
        options.uaConfig.mainThreadOnly = true;
        options.uaConfig.maxCalls = 1;
        options.uaConfig.userAgent = "AIPhone/0.1.0";
        options.logConfig.level = 0;
        options.logConfig.consoleLevel = 0;
        options.medConfig.clockRate = 8000;
        options.medConfig.sndClockRate = 8000;
        options.medConfig.ecTailLen = 0;
        options.medConfig.noVad = true;
        endpoint_.libInit(options);
        initialized_ = true;
        pj::TransportConfig transport;
        transport.port = config_.local_sip_port;
        transport.boundAddress = config_.local_ip;
        transport.publicAddress = config_.local_ip;
        const auto transport_id = endpoint_.transportCreate(PJSIP_TRANSPORT_UDP, transport);
        endpoint_.libStart();
        endpoint_.audDevManager().setNullDev();
        for (const auto &codec : endpoint_.codecEnum2()) {
            endpoint_.codecSetPriority(codec.codecId, codec.codecId == "PCMU/8000/1"   ? 255
                                                      : codec.codecId == "PCMA/8000/1" ? 254
                                                                                       : 0);
        }
        account_ = std::make_unique<RejectingAccount>();
        pj::AccountConfig account_config;
        account_config.idUri = "\"" + caller + "\" <sip:agent@" + config_.local_ip + ":" +
                               std::to_string(config_.local_sip_port) + ">";
        account_config.regConfig.registerOnAdd = false;
        account_config.sipConfig.transportId = transport_id;
        account_config.mediaConfig.transportConfig.port = config_.rtp_port;
        // Zero means unrestricted retry in PJSIP 2.17. A range of one keeps
        // every even RTP retry on this exact port and RTCP on the next one.
        account_config.mediaConfig.transportConfig.portRange = 1;
        account_config.mediaConfig.transportConfig.randomizePort = false;
        account_config.mediaConfig.transportConfig.boundAddress = config_.local_ip;
        account_config.mediaConfig.transportConfig.publicAddress = config_.local_ip;
        account_config.mediaConfig.srtpUse = PJMEDIA_SRTP_DISABLED;
        account_config.mediaConfig.lockCodecEnabled = false;
        account_config.natConfig.iceEnabled = false;
        account_config.natConfig.sipStunUse = PJSUA_STUN_USE_DISABLED;
        account_config.natConfig.mediaStunUse = PJSUA_STUN_USE_DISABLED;
        account_config.natConfig.contactRewriteUse = 0;
        account_config.natConfig.viaRewriteUse = 0;
        account_config.natConfig.sdpNatRewriteUse = 0;
        account_config.natConfig.udpKaIntervalSec = 0;
        if (!config_.auth_username.empty()) {
            account_config.sipConfig.authCreds.emplace_back(
                "digest", config_.auth_realm, config_.auth_username, 0, config_.auth_password);
        }
        account_->create(account_config);
        call_ = std::make_unique<OutgoingCall>(*account_, config_);
    }

    void run(const AudioSnapshot &audio, double audio_seconds) {
        pj::CallOpParam options(true);
        options.opt.audioCount = 1;
        options.opt.videoCount = 0;
        options.opt.textCount = 0;
        // Normal sendrecv SDP keeps this a normal phone call. Only the WAV is
        // connected to outgoing media; received audio has no sink or recorder.
        const auto dialed_at = Clock::now();
        const auto answer_deadline =
            dialed_at + std::chrono::seconds(config_.answer_timeout_seconds);
        call_->makeCall(config_.destination(), options);
        std::cerr << "Dialing " << config_.phone_ip << "...\n";
        Clock::time_point play_started{};
        Clock::time_point eof_at{};
        bool done = false;
        while (!done) {
            endpoint_.libHandleEvents(20);
            const auto now = Clock::now();
            result_.answered = call_->answered;
            if (call_->status >= 200) {
                result_.sip_status = call_->status;
            }
            if (cancellation_requested()) {
                decide("cancelled", 6, "Call cancelled locally.");
                break;
            }
            if (call_->failed || endpoint_.operation_error.load() != 0) {
                decide("media_failed", 5,
                       "Negotiated media or an asynchronous media operation failed.");
                break;
            }
            if (call_->answered && call_->answered_at > answer_deadline) {
                decide("no_answer", 4, "The answer arrived after the configured deadline.");
                break;
            }
            if (player_ && player_->eof.load() && !result_.playback_sent) {
                result_.playback_sent = true;
                eof_at = now;
            }
            if (call_->disconnected) {
                disconnected_result();
                break;
            }
            if (!call_->answered && now >= answer_deadline) {
                decide("no_answer", 4, "Phone was not answered before the deadline.");
                break;
            }
            if (call_->answered) {
                if (!call_->media_ready && !player_ &&
                    now - call_->answered_at >=
                        std::chrono::seconds(config_.media_ready_timeout_seconds)) {
                    decide("media_timeout", 5,
                           "Phone answered without usable outgoing audio media.");
                    break;
                }
                if (player_ && !call_->media_ready) {
                    decide("media_lost", 5, "Audio media was held or lost during playback.");
                    break;
                }
                if (!player_ && call_->media_ready && now - call_->answered_at >= 250ms) {
                    player_ = std::make_unique<Player>();
                    player_->createPlayer(native_filename(audio.path()), PJMEDIA_FILE_NO_LOOP);
                    sink_ = call_->getAudioMedia(call_->media_index);
                    endpoint_.source.store(player_->getPortId());
                    endpoint_.sink.store(sink_.getPortId());
                    player_->startTransmit(sink_);
                    play_started = now;
                    std::cerr << "Answered; sending audio.\n";
                }
                if (player_ && !endpoint_.connected.load() && now - play_started >= 2s) {
                    decide("media_timeout", 5, "Audio connection did not complete.");
                    break;
                }
                if (result_.playback_sent && now - eof_at >= 300ms) {
                    decide("completed", 0, "Audio sent and local call ended.");
                    done = true;
                }
                if (player_ &&
                    now - play_started > std::chrono::duration<double>(audio_seconds + 3.0)) {
                    decide("playback_timeout", 5, "Audio playback did not reach EOF on time.");
                    break;
                }
            }
            if (now - dialed_at > 162s) {
                decide("call_timeout", 5, "The overall call deadline was exceeded.");
                break;
            }
        }
    }

    void cleanup() noexcept {
        if (closed_) {
            return;
        }
        closed_ = true;
        bool okay = true;
        const auto safely = [&okay](auto &&action) {
            try {
                action();
            } catch (...) {
                okay = false;
            }
        };
        if (player_ && endpoint_.connected.load() && call_ && !call_->disconnected) {
            safely([&] { player_->stopTransmit(sink_); });
        }
        if (call_ && !call_->disconnected) {
            safely([&] {
                pj::CallOpParam end;
                call_->hangup(end);
                result_.hangup_requested = true;
            });
        }
        if (initialized_) {
            const auto deadline =
                Clock::now() + std::chrono::seconds(config_.shutdown_grace_seconds);
            safely([&] {
                while (Clock::now() < deadline) {
                    endpoint_.libHandleEvents(20);
                }
            });
            // Quiesce the media clock before releasing callback-bearing players,
            // including exceptional paths with pending conference operations.
            safely([&] { endpoint_.audDevManager().setNoDev(); });
        }
        if (call_) {
            result_.answered = result_.answered || call_->answered;
            if (call_->status >= 200) {
                result_.sip_status = call_->status;
            }
        }
        player_.reset();
        call_.reset();
        if (account_) {
            safely([&] { account_->shutdown(); });
        }
        account_.reset();
        if (created_) {
            safely([&] { endpoint_.libDestroy(PJSUA_DESTROY_NO_NETWORK); });
        }
        result_.local_cleanup_complete = okay;
        if (!okay) {
            decide("shutdown_failed", 5, "Local SIP/media shutdown reported an error.");
        }
    }

  private:
    void decide(const char *outcome, int code, const char *message) {
        result_.outcome = outcome;
        result_.exit_code = code;
        result_.message = message;
    }

    void disconnected_result() {
        if (result_.playback_sent && call_->status < 300) {
            decide("completed", 0, "Audio sent and remote call ended.");
        } else if (call_->answered) {
            decide("remote_hangup", 6, "Remote call ended before playback finished.");
        } else if (call_->status == 486 || call_->status == 600) {
            decide("phone_busy", 3, "Phone reported busy or DND.");
        } else if (call_->status == 401 || call_->status == 407 || call_->status == 403) {
            decide("authentication_failed", 5, "Phone rejected the SIP identity or credentials.");
        } else if (call_->status == 488 || call_->status == 415) {
            decide("codec_rejected", 5, "Phone rejected the offered audio media.");
        } else if (call_->status == 408 || call_->status == 480) {
            decide("no_answer", 4, "Phone was unavailable or did not answer.");
        } else if (call_->status >= 500 && call_->status != 603) {
            decide("network_failed", 5, "SIP transport or the remote endpoint reported a failure.");
        } else {
            decide("rejected", 4, "Phone rejected or ended the call before answer.");
        }
    }

    const Config &config_;
    Result &result_;
    Endpoint endpoint_;
    std::unique_ptr<RejectingAccount> account_;
    std::unique_ptr<OutgoingCall> call_;
    std::unique_ptr<Player> player_;
    pj::AudioMedia sink_;
    bool created_ = false;
    bool initialized_ = false;
    bool closed_ = false;
};
} // namespace

Result place_call(const Config &config, const AudioSnapshot &audio, double audio_seconds,
                  const std::string &caller, const std::string &request_id) {
    NativeOutput diagnostics;
    Result result;
    result.request_id = request_id;
    Session session(config, result);
    try {
        session.start(caller);
        session.run(audio, audio_seconds);
    } catch (const pj::Error &error) {
        result.outcome = "sip_failed";
        result.exit_code = 5;
        result.message =
            "SIP/media operation failed with native status " + std::to_string(error.status) + ".";
    } catch (const std::exception &) {
        result.outcome = "internal_error";
        result.exit_code = 5;
        result.message = "The caller could not complete the operation.";
    }
    session.cleanup();
    return result;
}
} // namespace aiphone
