#include "audio.hpp"
#include "config.hpp"
#include "platform.hpp"
#include "result.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dr_wav.h>
#include <fstream>
#include <future>
#include <memory>

using namespace aiphone;
using nlohmann::json;

namespace {
// Synthetic addresses for parsing tests; these fixtures never place a call.
json valid_config() {
    return {{"schema_version", 1},
            {"phone_ip", "192.168.0.20"},
            {"local_ip", "192.168.0.10"},
            {"phone_user", ""}};
}

std::vector<unsigned char> wav(unsigned rate = 8000, unsigned channels = 1, unsigned frames = 80) {
    drwav writer{};
    void *buffer = nullptr;
    std::size_t size = 0;
    drwav_data_format format{drwav_container_riff, DR_WAVE_FORMAT_PCM, channels, rate, 16};
    REQUIRE(drwav_init_memory_write(&writer, &buffer, &size, &format, nullptr));
    std::vector<std::int16_t> samples(frames * channels, 1234);
    REQUIRE(drwav_write_pcm_frames(&writer, frames, samples.data()) == frames);
    REQUIRE(drwav_uninit(&writer) == DRWAV_SUCCESS);
    std::vector<unsigned char> result(static_cast<unsigned char *>(buffer),
                                      static_cast<unsigned char *>(buffer) + size);
    drwav_free(buffer, nullptr);
    return result;
}

struct TestDirectory {
    std::filesystem::path path = private_root() / wide("test-" + unique_id());
    TestDirectory() {
        std::filesystem::create_directory(path);
    }
    ~TestDirectory() {
        std::error_code error;
        std::filesystem::remove(path / L"ää message.wav", error);
        std::filesystem::remove(path, error);
    }
};
} // namespace

TEST_CASE("Only literal private IPv4 destinations are accepted") {
    for (const auto *address : {"10.1.2.3", "172.16.0.1", "172.31.255.254", "192.168.0.20"}) {
        REQUIRE(is_private_ipv4(address));
    }
    for (const auto *address :
         {"127.0.0.1", "0.0.0.0", "172.15.0.1", "172.32.0.1", "8.8.8.8", "169.254.1.2",
          "phone.local", "192.168.0.20;transport=tcp", "192.168.0.20\r\n", "::1"}) {
        REQUIRE_FALSE(is_private_ipv4(address));
    }
}

TEST_CASE("Configuration is strict and constructs a bounded direct URI") {
    auto document = valid_config();
    REQUIRE(parse_config(document).destination() == "sip:192.168.0.20:5060");
    document["phone_user"] = "101";
    REQUIRE(parse_config(document).destination() == "sip:101@192.168.0.20:5060");
    document["phone_user"] = "101@other.host";
    REQUIRE_THROWS_AS(parse_config(document), InputError);
    document = valid_config();
    document["unknown"] = true;
    REQUIRE_THROWS_AS(parse_config(document), InputError);
    document = valid_config();
    document.erase("schema_version");
    REQUIRE_THROWS_AS(parse_config(document), InputError);
    document = valid_config();
    document["schema_version"] = 2;
    REQUIRE_THROWS_AS(parse_config(document), InputError);
    REQUIRE_THROWS_AS(parse_config(json::array()), InputError);
}

TEST_CASE("Limits reject negative fractional excessive and mistyped values") {
    for (const auto &value :
         {json(-1), json(0), json(31), json(1.5), json("30"), json(true), json(nullptr)}) {
        auto document = valid_config();
        document["answer_timeout_seconds"] = value;
        REQUIRE_THROWS_AS(parse_config(document), InputError);
    }
    for (const auto *field :
         {"max_audio_seconds", "media_ready_timeout_seconds", "shutdown_grace_seconds"}) {
        auto document = valid_config();
        document[field] = 10000;
        REQUIRE_THROWS_AS(parse_config(document), InputError);
    }
}

TEST_CASE("One fixed RTP and RTCP pair must be separate from signaling") {
    for (const auto &change :
         {json{{"rtp_port", 40001}}, json{{"rtcp_port", 40002}}, json{{"local_sip_port", 40000}},
          json{{"rtp_port", 65535}}, json{{"phone_sip_port", 70000}}}) {
        auto document = valid_config();
        document.update(change);
        REQUIRE_THROWS_AS(parse_config(document), InputError);
    }
}

TEST_CASE("Optional credentials are validated without being echoed") {
    auto document = valid_config();
    document["auth"] = {{"username", "agent"}, {"password", "test-only-password"}};
    REQUIRE(parse_config(document).auth_realm == "*");
    document["auth"]["password"] = "DO-NOT-ECHO\r\n";
    try {
        (void)parse_config(document);
        FAIL("Newline credentials should be rejected");
    } catch (const InputError &error) {
        REQUIRE(std::string(error.what()).find("DO-NOT-ECHO") == std::string::npos);
    }
}

TEST_CASE("Caller and request labels cannot inject SIP headers") {
    REQUIRE_NOTHROW(validate_caller("MY AI-AGENT_2"));
    for (const auto *name : {"", "   ", "Agent\r\nFrom: attacker", "Agent\"", "ää", "Agent@host",
                             "1234567890123456789012345"}) {
        REQUIRE_THROWS_AS(validate_caller(name), InputError);
    }
    REQUIRE_NOTHROW(validate_request_id("project.task-001"));
    REQUIRE_THROWS_AS(validate_request_id("a b"), InputError);
    REQUIRE_THROWS_AS(validate_request_id(std::string(129, 'x')), InputError);
}

TEST_CASE("WAV policy accepts PCM16 mono 8k and preserves samples") {
    const auto audio = decode_audio(wav(), 120);
    REQUIRE(audio.samples.size() == 80);
    REQUIRE(audio.samples.front() == 1234);
    REQUIRE(audio.samples.back() == 1234);
    REQUIRE(audio.seconds() == 0.01);
    REQUIRE_THROWS_AS(decode_audio(wav(22050), 120), InputError);
    REQUIRE_THROWS_AS(decode_audio(wav(8000, 2), 120), InputError);
    REQUIRE_THROWS_AS(decode_audio(wav(8000, 1, 0), 120), InputError);
    REQUIRE_THROWS_AS(decode_audio(wav(8000, 1, 8001), 1), InputError);
}

TEST_CASE("Malformed truncated and excessive WAV containers are rejected") {
    REQUIRE_THROWS_AS(decode_audio({}, 120), InputError);
    auto bytes = wav();
    bytes.resize(bytes.size() - 1);
    REQUIRE_THROWS_AS(decode_audio(bytes, 120), InputError);
    bytes = wav();
    bytes[0] = 'X';
    REQUIRE_THROWS_AS(decode_audio(bytes, 120), InputError);
    REQUIRE_THROWS_AS(decode_audio(std::vector<unsigned char>(2 * 1024 * 1024 + 1), 120),
                      InputError);
}

TEST_CASE("Unicode producer files are unchanged and snapshots are independent") {
    TestDirectory temp;
    const auto source = temp.path / L"ää message.wav";
    const auto bytes = wav();
    {
        std::ofstream stream(source, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    const auto audio = read_audio(source, 120);
    AudioSnapshot snapshot(audio);
    REQUIRE(std::filesystem::exists(source));
    REQUIRE(std::filesystem::file_size(source) == bytes.size());
    std::filesystem::remove(source);
    REQUIRE(read_audio(snapshot.path(), 120).samples == audio.samples);
    REQUIRE(snapshot.cleanup());
    REQUIRE(snapshot.cleanup());
    REQUIRE_FALSE(std::filesystem::exists(snapshot.path()));
}

TEST_CASE("Missing and network file paths are not accepted") {
    REQUIRE_THROWS_AS(local_file(L"Z:\\this-file-does-not-exist.wav"), InputError);
    REQUIRE_THROWS_AS(local_file(L"\\\\127.0.0.1\\C$\\file.wav"), InputError);
}

TEST_CASE("Concurrent caller threads cannot acquire the same phone mutex") {
    const auto key = unique_id();
    {
        SingleInstance first(key);
        REQUIRE(first.acquired());
        auto competing = std::async(std::launch::async, [&] {
            SingleInstance second(key);
            return second.acquired();
        });
        REQUIRE_FALSE(competing.get());
    }
    SingleInstance after(key);
    REQUIRE(after.acquired());
}

TEST_CASE("Results use a stable typed JSON envelope without a false remote guarantee") {
    Result result;
    const auto value = result.json();
    REQUIRE(value.at("schema_version") == 1);
    REQUIRE(value.at("sip_status").is_null());
    REQUIRE(value.at("playback_sent").is_boolean());
    REQUIRE(value.at("local_cleanup_complete").is_boolean());
    REQUIRE_FALSE(value.contains("teardown_confirmed"));
    REQUIRE_FALSE(value.contains("heard"));
    result.sip_status = 486;
    REQUIRE(result.json().at("sip_status") == 486);
}
