#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace aiphone {

class InputError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct Config {
    std::string phone_ip;
    std::string phone_user;
    std::string local_ip;
    unsigned phone_sip_port = 5060;
    unsigned local_sip_port = 5062;
    unsigned rtp_port = 40000;
    unsigned rtcp_port = 40001;
    unsigned answer_timeout_seconds = 30;
    unsigned max_audio_seconds = 120;
    unsigned media_ready_timeout_seconds = 5;
    unsigned shutdown_grace_seconds = 5;
    std::string auth_username;
    std::string auth_password;
    std::string auth_realm = "*";

    std::string destination() const;
};

Config parse_config(const nlohmann::json &document);
Config read_config(const std::filesystem::path &path);
void validate_caller(const std::string &name);
void validate_request_id(const std::string &value);
bool is_private_ipv4(const std::string &value);

} // namespace aiphone
