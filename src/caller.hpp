#pragma once

#include "audio.hpp"
#include "config.hpp"
#include "result.hpp"

namespace aiphone {
Result place_call(const Config &config, const AudioSnapshot &audio, double audio_seconds,
                  const std::string &caller, const std::string &request_id);
}
