#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace aiphone {

struct Result {
    std::string request_id;
    std::string outcome = "internal_error";
    std::string message;
    bool answered = false;
    bool playback_sent = false;
    bool hangup_requested = false;
    bool local_cleanup_complete = false;
    int sip_status = 0;
    int exit_code = 5;
    long long elapsed_ms = 0;

    nlohmann::json json() const {
        return {
            {"schema_version", 1},
            {"request_id", request_id},
            {"outcome", outcome},
            {"message", message},
            {"answered", answered},
            {"playback_sent", playback_sent},
            {"hangup_requested", hangup_requested},
            {"local_cleanup_complete", local_cleanup_complete},
            {"sip_status", sip_status == 0 ? nlohmann::json(nullptr) : nlohmann::json(sip_status)},
            {"elapsed_ms", elapsed_ms},
            {"exit_code", exit_code}};
    }
};

} // namespace aiphone
