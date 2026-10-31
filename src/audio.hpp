#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace aiphone {

struct AudioData {
    std::vector<std::int16_t> samples;
    double seconds() const {
        return static_cast<double>(samples.size()) / 8000.0;
    }
};

AudioData read_audio(const std::filesystem::path &path, unsigned maximum_seconds);
AudioData decode_audio(const std::vector<unsigned char> &bytes, unsigned maximum_seconds);

class AudioSnapshot {
  public:
    explicit AudioSnapshot(const AudioData &data);
    ~AudioSnapshot();
    bool cleanup() noexcept;
    const std::filesystem::path &path() const {
        return file_;
    }
    AudioSnapshot(const AudioSnapshot &) = delete;
    AudioSnapshot &operator=(const AudioSnapshot &) = delete;

  private:
    std::filesystem::path directory_;
    std::filesystem::path file_;
};

} // namespace aiphone
