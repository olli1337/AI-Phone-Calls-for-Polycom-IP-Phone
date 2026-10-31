#include "audio.hpp"
#include "caller.hpp"
#include "config.hpp"
#include "platform.hpp"
#include "result.hpp"
#include <CLI/CLI.hpp>
#include <chrono>
#include <iostream>

int main(int argc, char **argv) {
    using namespace aiphone;
    const auto started = std::chrono::steady_clock::now();
    CLI::App app{"Play a WAV in a direct LAN call to a Polycom VVX 301."};
    argv = app.ensure_utf8(argv);
    app.set_version_flag("--version", "AI Phone Calls for Polycom IP Phone 0.1.0 (PJSIP 2.17)");
    app.require_subcommand(1, 1);
    auto *call =
        app.add_subcommand("call", "Ring the configured phone and play audio after answer.");
    auto *doctor =
        app.add_subcommand("doctor", "Check local prerequisites without sending SIP or ringing.");
    std::string config_path;
    std::string audio_path;
    std::string caller = "AI AGENT";
    std::string request_id;
    bool json = false;
    call->add_option("--config", config_path, "Local configuration JSON")->required();
    call->add_option("--audio", audio_path, "PCM16 mono 8000 Hz WAV, at most 120 seconds")
        ->required();
    call->add_option("--caller", caller, "Caller display name (1-24 ASCII characters)");
    call->add_option("--request-id", request_id, "Optional correlation label (not deduplication)");
    call->add_flag("--json", json, "Print one structured JSON result");
    doctor->add_option("--config", config_path, "Local configuration JSON")->required();
    doctor->add_option("--audio", audio_path, "Optionally validate a WAV too");
    doctor->add_flag("--json", json, "Print one structured JSON result");
    bool requested_json = false;
    for (int index = 1; index < argc; ++index) {
        requested_json = requested_json || std::string(argv[index]) == "--json";
    }
    Result result;
    try {
        app.parse(argc, argv);
        validate_request_id(request_id);
        result.request_id = request_id.empty() ? unique_id() : request_id;
        validate_caller(caller);
        NetworkRuntime network;
        const auto config = read_config(wide(config_path));
        SingleInstance instance(config.phone_ip);
        if (!instance.acquired()) {
            result.outcome = "local_busy";
            result.exit_code = 3;
            result.message = "Another caller is already running for this phone.";
            result.local_cleanup_complete = true;
        } else {
            check_local_ports(config);
            if (*doctor) {
                if (!audio_path.empty()) {
                    (void)read_audio(wide(audio_path), config.max_audio_seconds);
                }
                result.outcome = "ready";
                result.exit_code = 0;
                result.local_cleanup_complete = true;
                result.message =
                    "Local configuration and ports are ready; phone reachability was not tested.";
            } else {
                ConsoleCancellation cancellation;
                const auto audio = read_audio(wide(audio_path), config.max_audio_seconds);
                AudioSnapshot snapshot(audio);
                result = place_call(config, snapshot, audio.seconds(), caller, result.request_id);
                if (!snapshot.cleanup()) {
                    result.outcome = "shutdown_failed";
                    result.message = "The private audio snapshot could not be removed.";
                    result.exit_code = 5;
                    result.local_cleanup_complete = false;
                }
            }
        }
    } catch (const CLI::ParseError &error) {
        if (error.get_exit_code() == 0 || !requested_json) {
            return app.exit(error);
        }
        result.outcome = "invalid_input";
        result.message = error.what();
        result.exit_code = 2;
        result.local_cleanup_complete = true;
    } catch (const InputError &error) {
        result.outcome = "invalid_input";
        result.message = error.what();
        result.exit_code = 2;
        result.local_cleanup_complete = true;
    } catch (const std::exception &) {
        result.outcome = "internal_error";
        result.message = "The operation failed before a call could complete.";
        result.exit_code = 5;
    }
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count();
    if (json || requested_json) {
        std::cout << result.json().dump() << '\n';
    } else {
        std::cout << result.outcome << ": " << result.message << '\n';
    }
    return result.exit_code;
}
