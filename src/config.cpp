#include "config.hpp"
#include "platform.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <ws2tcpip.h>

namespace aiphone {
namespace {
bool identifier_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.';
}

void known_fields(const nlohmann::json &object, const std::set<std::string> &fields) {
    if (!object.is_object()) {
        throw InputError("Configuration must be a JSON object.");
    }
    for (const auto &item : object.items()) {
        if (fields.count(item.key()) == 0) {
            throw InputError("Configuration contains an unsupported field.");
        }
    }
}

unsigned number(const nlohmann::json &document, const char *name, unsigned fallback, unsigned low,
                unsigned high) {
    if (!document.contains(name)) {
        return fallback;
    }
    const auto &value = document.at(name);
    if (!value.is_number_integer() || value.get<double>() < low || value.get<double>() > high) {
        throw InputError(std::string("Invalid configuration limit or port: ") + name);
    }
    return value.get<unsigned>();
}

std::string text(const nlohmann::json &document, const char *name, std::string fallback = {}) {
    if (!document.contains(name)) {
        return fallback;
    }
    if (!document.at(name).is_string()) {
        throw InputError("A configuration string has the wrong type.");
    }
    return document.at(name).get<std::string>();
}
} // namespace

bool is_private_ipv4(const std::string &value) {
    IN_ADDR address{};
    if (InetPtonA(AF_INET, value.c_str(), &address) != 1) {
        return false;
    }
    const auto host = ntohl(address.s_addr);
    return (host >> 24U) == 10U || (host >> 20U) == 0xAC1U || (host >> 16U) == 0xC0A8U;
}

Config parse_config(const nlohmann::json &document) {
    known_fields(document, {"schema_version", "phone_ip", "phone_user", "phone_sip_port",
                            "local_ip", "local_sip_port", "rtp_port", "rtcp_port",
                            "answer_timeout_seconds", "max_audio_seconds",
                            "media_ready_timeout_seconds", "shutdown_grace_seconds", "auth"});
    if (number(document, "schema_version", 0, 1, 1) != 1) {
        throw InputError("Configuration schema_version must be 1.");
    }
    Config config;
    config.phone_ip = text(document, "phone_ip");
    config.local_ip = text(document, "local_ip");
    config.phone_user = text(document, "phone_user");
    if (!is_private_ipv4(config.phone_ip) || !is_private_ipv4(config.local_ip)) {
        throw InputError("phone_ip and local_ip must be literal private LAN IPv4 addresses.");
    }
    if (config.phone_user.size() > 64 ||
        !std::all_of(config.phone_user.begin(), config.phone_user.end(), identifier_char)) {
        throw InputError("phone_user must be empty or contain only letters, digits, _, - and .");
    }
    config.phone_sip_port = number(document, "phone_sip_port", 5060, 1024, 65535);
    config.local_sip_port = number(document, "local_sip_port", 5062, 1024, 65535);
    config.rtp_port = number(document, "rtp_port", 40000, 1024, 65534);
    config.rtcp_port = number(document, "rtcp_port", 40001, 1025, 65535);
    if (config.rtp_port % 2 != 0 || config.rtcp_port != config.rtp_port + 1 ||
        config.local_sip_port == config.rtp_port || config.local_sip_port == config.rtcp_port) {
        throw InputError("RTP must use an even port, RTCP the next port, and SIP a separate port.");
    }
    config.answer_timeout_seconds = number(document, "answer_timeout_seconds", 30, 1, 30);
    config.max_audio_seconds = number(document, "max_audio_seconds", 120, 1, 120);
    config.media_ready_timeout_seconds = number(document, "media_ready_timeout_seconds", 5, 1, 5);
    config.shutdown_grace_seconds = number(document, "shutdown_grace_seconds", 5, 1, 5);
    if (document.contains("auth")) {
        const auto &auth = document.at("auth");
        known_fields(auth, {"username", "password", "realm"});
        config.auth_username = text(auth, "username");
        config.auth_password = text(auth, "password");
        config.auth_realm = text(auth, "realm", "*");
        for (const auto *field :
             {&config.auth_username, &config.auth_password, &config.auth_realm}) {
            if (field->empty() || field->size() > 512 ||
                field->find_first_of("\r\n\0", 0, 3) != std::string::npos) {
                throw InputError("SIP authentication requires bounded, nonempty credentials.");
            }
        }
    }
    return config;
}

Config read_config(const std::filesystem::path &path) {
    const auto file = local_file(path);
    if (std::filesystem::file_size(file) > 16384) {
        throw InputError("Configuration exceeds 16 KiB.");
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        throw InputError("Cannot open configuration file.");
    }
    // A bounded read also handles a file growing between stat and open.
    std::string contents(16385, '\0');
    stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    contents.resize(static_cast<std::size_t>(stream.gcount()));
    if (contents.size() > 16384) {
        throw InputError("Configuration exceeds 16 KiB.");
    }
    try {
        return parse_config(nlohmann::json::parse(contents));
    } catch (const nlohmann::json::exception &) {
        throw InputError("Configuration is not valid JSON with the documented field types.");
    }
}

std::string Config::destination() const {
    return "sip:" + (phone_user.empty() ? "" : phone_user + "@") + phone_ip + ":" +
           std::to_string(phone_sip_port);
}

void validate_caller(const std::string &name) {
    if (name.empty() || name.size() > 24 ||
        !std::all_of(name.begin(), name.end(),
                     [](unsigned char c) {
                         return c == ' ' || c == '_' || c == '-' || (c >= 'a' && c <= 'z') ||
                                (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
                     }) ||
        name.find_first_not_of(' ') == std::string::npos) {
        throw InputError("Caller name must contain 1-24 ASCII letters, digits, spaces, _ or -.");
    }
}

void validate_request_id(const std::string &value) {
    if (value.size() > 128 || !std::all_of(value.begin(), value.end(), identifier_char)) {
        throw InputError("Request ID must use at most 128 ASCII letters, digits, _, - or .");
    }
}
} // namespace aiphone
