#pragma once

#include "config.hpp"
#include <filesystem>
#include <string>
#include <windows.h>
#include <winsock2.h>

namespace aiphone {

std::wstring wide(const std::string &utf8);
std::string utf8(const std::wstring &text);
std::string native_filename(const std::filesystem::path &path);
std::string unique_id();
std::filesystem::path private_root();
std::filesystem::path local_file(const std::filesystem::path &path);
void check_local_ports(const Config &config);
bool cancellation_requested();

class NetworkRuntime {
  public:
    NetworkRuntime();
    ~NetworkRuntime();
    NetworkRuntime(const NetworkRuntime &) = delete;
    NetworkRuntime &operator=(const NetworkRuntime &) = delete;
};

class ConsoleCancellation {
  public:
    ConsoleCancellation();
    ~ConsoleCancellation();
};

class SingleInstance {
  public:
    explicit SingleInstance(const std::string &phone_ip);
    ~SingleInstance();
    bool acquired() const {
        return acquired_;
    }
    SingleInstance(const SingleInstance &) = delete;
    SingleInstance &operator=(const SingleInstance &) = delete;

  private:
    HANDLE mutex_ = nullptr;
    bool acquired_ = false;
};

} // namespace aiphone
