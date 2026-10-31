#include "audio.hpp"
#include "config.hpp"
#include "platform.hpp"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include <algorithm>
#include <chrono>
#include <cstring>
#include <dr_wav.h>
#include <fstream>
#include <memory>
#include <regex>

namespace aiphone {
namespace {
constexpr std::size_t maximum_bytes = 2 * 1024 * 1024;

void remove_old_snapshots(const std::filesystem::path &root) {
    // No recursive deletion: only our fixed audio filename and then an empty
    // GUID-named directory. Reparse points and unexpected contents are left alone.
    const std::wregex expected(L"call-[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
                               L"[0-9a-fA-F]{4}-[0-9a-fA-F]{12}");
    std::error_code error;
    std::filesystem::directory_iterator entries(root, error);
    unsigned inspected = 0;
    for (const auto &entry : entries) {
        if (++inspected > 256) {
            break;
        }
        const auto path = entry.path();
        if (path.parent_path() != root || !std::regex_match(path.filename().native(), expected)) {
            continue;
        }
        const auto flags = GetFileAttributesW(path.c_str());
        if (flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) ||
            (flags & FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;
        }
        const auto modified = std::filesystem::last_write_time(path, error);
        if (error ||
            std::filesystem::file_time_type::clock::now() - modified < std::chrono::hours(24)) {
            error.clear();
            continue;
        }
        const auto audio = path / L"audio.wav";
        const auto audio_flags = GetFileAttributesW(audio.c_str());
        if (audio_flags != INVALID_FILE_ATTRIBUTES &&
            !(audio_flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            DeleteFileW(audio.c_str());
        }
        RemoveDirectoryW(path.c_str());
    }
}
} // namespace

AudioData decode_audio(const std::vector<unsigned char> &bytes, unsigned maximum_seconds) {
    if (bytes.size() < 44 || bytes.size() > maximum_bytes ||
        std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        throw InputError("Audio must be a RIFF/WAVE file no larger than 2 MiB.");
    }
    const auto container_size = static_cast<std::uint32_t>(bytes[4]) |
                                (static_cast<std::uint32_t>(bytes[5]) << 8U) |
                                (static_cast<std::uint32_t>(bytes[6]) << 16U) |
                                (static_cast<std::uint32_t>(bytes[7]) << 24U);
    if (static_cast<std::uint64_t>(container_size) + 8 != bytes.size()) {
        throw InputError("WAV container size does not match the file; it may be truncated.");
    }
    drwav decoder{};
    if (!drwav_init_memory(&decoder, bytes.data(), bytes.size(), nullptr)) {
        throw InputError("WAV audio is malformed or unsupported.");
    }
    struct DecoderGuard {
        drwav *decoder;
        ~DecoderGuard() {
            drwav_uninit(decoder);
        }
    } guard{&decoder};
    if (decoder.container != drwav_container_riff ||
        decoder.translatedFormatTag != DR_WAVE_FORMAT_PCM ||
        decoder.fmt.formatTag != DR_WAVE_FORMAT_PCM || decoder.bitsPerSample != 16 ||
        decoder.channels != 1 || decoder.sampleRate != 8000 || decoder.totalPCMFrameCount == 0 ||
        decoder.totalPCMFrameCount > static_cast<drwav_uint64>(maximum_seconds) * 8000) {
        throw InputError(
            "Use nonempty PCM16 mono 8000 Hz WAV audio within the configured duration.");
    }
    AudioData audio;
    audio.samples.resize(static_cast<std::size_t>(decoder.totalPCMFrameCount));
    if (drwav_read_pcm_frames_s16(&decoder, decoder.totalPCMFrameCount, audio.samples.data()) !=
        decoder.totalPCMFrameCount) {
        throw InputError("The WAV sample data is truncated.");
    }
    return audio;
}

AudioData read_audio(const std::filesystem::path &path, unsigned maximum_seconds) {
    const auto file = local_file(path);
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        throw InputError("Cannot open the supplied audio file.");
    }
    std::vector<unsigned char> bytes(maximum_bytes + 1);
    stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(stream.gcount()));
    return decode_audio(bytes, maximum_seconds);
}

AudioSnapshot::AudioSnapshot(const AudioData &data) {
    const auto root = private_root();
    remove_old_snapshots(root);
    directory_ = root / wide("call-" + unique_id());
    if (!CreateDirectoryW(directory_.c_str(), nullptr)) {
        throw std::runtime_error("Cannot create the private playback snapshot.");
    }
    file_ = directory_ / L"audio.wav";
    try {
        drwav writer{};
        void *buffer = nullptr;
        std::size_t size = 0;
        drwav_data_format format{drwav_container_riff, DR_WAVE_FORMAT_PCM, 1, 8000, 16};
        if (!drwav_init_memory_write(&writer, &buffer, &size, &format, nullptr)) {
            throw std::runtime_error("Cannot prepare canonical playback audio.");
        }
        const auto written =
            drwav_write_pcm_frames(&writer, data.samples.size(), data.samples.data());
        const auto status = drwav_uninit(&writer);
        std::unique_ptr<void, void (*)(void *)> owner(
            buffer, [](void *value) { drwav_free(value, nullptr); });
        if (written != data.samples.size() || status != DRWAV_SUCCESS) {
            throw std::runtime_error("Cannot finish canonical playback audio.");
        }
        std::ofstream stream(file_, std::ios::binary | std::ios::trunc);
        stream.write(static_cast<const char *>(buffer), static_cast<std::streamsize>(size));
        stream.close();
        if (!stream) {
            throw std::runtime_error("Cannot write the private playback snapshot.");
        }
        (void)native_filename(file_);
    } catch (...) {
        DeleteFileW(file_.c_str());
        RemoveDirectoryW(directory_.c_str());
        throw;
    }
}

AudioSnapshot::~AudioSnapshot() {
    cleanup();
}

bool AudioSnapshot::cleanup() noexcept {
    const auto file_deleted = DeleteFileW(file_.c_str());
    const auto file_error = GetLastError();
    const auto file_removed =
        file_deleted || file_error == ERROR_FILE_NOT_FOUND || file_error == ERROR_PATH_NOT_FOUND;
    const auto directory_removed = RemoveDirectoryW(directory_.c_str()) ||
                                   GetLastError() == ERROR_PATH_NOT_FOUND ||
                                   GetLastError() == ERROR_FILE_NOT_FOUND;
    return file_removed && directory_removed;
}
} // namespace aiphone
