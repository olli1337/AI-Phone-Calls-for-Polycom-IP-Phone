// Hardware-free SIP fixture. It only records synthetic test audio from a local
// caller; it never opens a microphone or a playback device.
#include "platform.hpp"
#include <CLI/CLI.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <pj/log.h>
#include <pjsua2.hpp>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {
struct Options {
    std::string ip;
    unsigned port = 5064;
    unsigned rtp = 40002;
    std::string mode = "answer";
    std::string recording;
};

void log_stderr(int, const char *text, int size) {
    std::cerr.write(text, size);
}

class TestCall final : public pj::Call {
  public:
    TestCall(pj::Account &account, int id, const Options &options)
        : pj::Call(account, id), options_(options) {}

    bool ended = false;
    bool recording = false;
    pj::AudioMediaRecorder recorder;

    void onCallState(pj::OnCallStateParam &) override {
        const auto info = getInfo();
        ended = info.state == PJSIP_INV_STATE_DISCONNECTED;
        std::cerr << "STATE " << info.stateText << '\n';
    }

    void onCallMediaState(pj::OnCallMediaStateParam &) override {
        if (recording || options_.recording.empty()) {
            return;
        }
        const auto info = getInfo();
        for (const auto &media : info.media) {
            if (media.type == PJMEDIA_TYPE_AUDIO && media.status == PJSUA_CALL_MEDIA_ACTIVE &&
                (media.dir & PJMEDIA_DIR_DECODING)) {
                recorder.createRecorder(options_.recording);
                getAudioMedia(static_cast<int>(media.index)).startTransmit(recorder);
                recording = true;
                break;
            }
        }
    }

    void onCallSdpCreated(pj::OnCallSdpCreatedParam &event) override {
        if (options_.mode == "bad-media") {
            auto &sdp = event.sdp.wholeSdp;
            const auto target = "c=IN IP4 " + options_.ip;
            const auto position = sdp.find(target);
            if (position != std::string::npos) {
                sdp.replace(position, target.size(), "c=IN IP4 127.0.0.1");
            }
        }
    }

  private:
    const Options &options_;
};

class TestAccount final : public pj::Account {
  public:
    explicit TestAccount(const Options &options) : options_(options) {}
    std::unique_ptr<TestCall> call;
    Clock::time_point incoming_at{};
    Clock::time_point answered_at{};
    bool answered = false;
    bool altered = false;

    void onIncomingCall(pj::OnIncomingCallParam &event) override {
        incoming_at = Clock::now();
        call = std::make_unique<TestCall>(*this, event.callId, options_);
        pj::CallOpParam response(true);
        response.opt.audioCount = 1;
        response.opt.videoCount = 0;
        response.opt.textCount = 0;
        response.statusCode = options_.mode == "busy"       ? PJSIP_SC_BUSY_HERE
                              : options_.mode == "reject"   ? PJSIP_SC_DECLINE
                              : options_.mode == "redirect" ? PJSIP_SC_MOVED_TEMPORARILY
                              : options_.mode == "early"    ? PJSIP_SC_PROGRESS
                                                            : PJSIP_SC_RINGING;
        if (options_.mode == "redirect") {
            pj::SipHeader contact;
            contact.hName = "Contact";
            contact.hValue = "<sip:other@" + options_.ip + ":5066>";
            response.txOption.headers.push_back(contact);
        }
        call->answer(response);
    }

  private:
    const Options &options_;
};
} // namespace

int main(int argc, char **argv) {
    CLI::App app{"Local synthetic-audio SIP test fixture"};
    argv = app.ensure_utf8(argv);
    Options options;
    app.add_option("--ip", options.ip)->required();
    app.add_option("--port", options.port);
    app.add_option("--rtp", options.rtp);
    app.add_option("--mode", options.mode);
    app.add_option("--record", options.recording);
    CLI11_PARSE(app, argc, argv);
    pj_log_set_level(0);
    pj_log_set_log_func(log_stderr);
    try {
        pj::Endpoint endpoint;
        endpoint.libCreate();
        pj::EpConfig config;
        config.uaConfig.threadCnt = 0;
        config.uaConfig.mainThreadOnly = true;
        config.logConfig.level = 0;
        config.logConfig.consoleLevel = 0;
        config.medConfig.clockRate = 8000;
        config.medConfig.ecTailLen = 0;
        config.medConfig.noVad = true;
        endpoint.libInit(config);
        pj::TransportConfig transport;
        transport.port = options.port;
        transport.boundAddress = options.ip;
        transport.publicAddress = options.ip;
        endpoint.transportCreate(PJSIP_TRANSPORT_UDP, transport);
        endpoint.libStart();
        endpoint.audDevManager().setNullDev();
        const auto selected_codec = options.mode == "pcma" ? "PCMA/8000/1" : "PCMU/8000/1";
        for (const auto &codec : endpoint.codecEnum2()) {
            endpoint.codecSetPriority(codec.codecId, codec.codecId == selected_codec ? 255 : 0);
        }
        TestAccount account(options);
        pj::AccountConfig account_config;
        account_config.idUri = "sip:testpeer@" + options.ip + ":" + std::to_string(options.port);
        account_config.regConfig.registerOnAdd = false;
        account_config.mediaConfig.transportConfig.port = options.rtp;
        account_config.mediaConfig.transportConfig.portRange = 1;
        account_config.mediaConfig.transportConfig.boundAddress = options.ip;
        account_config.mediaConfig.transportConfig.publicAddress = options.ip;
        account_config.mediaConfig.lockCodecEnabled = false;
        account.create(account_config);
        std::cout << "READY\n" << std::flush;
        const auto started = Clock::now();
        while (Clock::now() - started < 20s) {
            endpoint.libHandleEvents(20);
            const auto now = Clock::now();
            if (account.call && account.call->ended) {
                break;
            }
            if (account.call && !account.answered && options.mode != "busy" &&
                options.mode != "reject" && options.mode != "redirect" &&
                options.mode != "silent" && now - account.incoming_at >= 1500ms) {
                pj::CallOpParam answer(true);
                answer.opt.audioCount = 1;
                answer.opt.videoCount = 0;
                answer.opt.textCount = 0;
                answer.statusCode = PJSIP_SC_OK;
                account.call->answer(answer);
                account.answered = true;
                account.answered_at = now;
                std::cerr << "ANSWERED\n";
            }
            if (account.answered && !account.altered && now - account.answered_at >= 1s) {
                if (options.mode == "hangup") {
                    pj::CallOpParam end;
                    account.call->hangup(end);
                    account.altered = true;
                } else if (options.mode == "hold") {
                    pj::CallOpParam hold(true);
                    hold.opt.textCount = 0;
                    account.call->setHold(hold);
                    account.altered = true;
                }
            }
        }
        const auto grace = Clock::now();
        while (Clock::now() - grace < 300ms) {
            endpoint.libHandleEvents(20);
        }
        endpoint.audDevManager().setNoDev();
        account.call.reset();
        account.shutdown();
        endpoint.libDestroy(PJSUA_DESTROY_NO_NETWORK);
        return 0;
    } catch (const pj::Error &error) {
        std::cerr << "Test peer native error " << error.status << '\n';
        return 5;
    }
}
