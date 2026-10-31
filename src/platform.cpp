#include "platform.hpp"
#include <objbase.h>
#include <sddl.h>
#include <vector>
#include <ws2tcpip.h>

namespace aiphone {
namespace {
volatile LONG cancelled = 0;

BOOL WINAPI console_handler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        InterlockedExchange(&cancelled, 1);
        return TRUE;
    }
    return FALSE;
}

std::wstring user_sid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        throw std::runtime_error("Cannot inspect the current Windows user.");
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> storage(size);
    const auto okay = GetTokenInformation(token, TokenUser, storage.data(), size, &size);
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!okay ||
        !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(storage.data())->User.Sid, &sid)) {
        throw std::runtime_error("Cannot identify the current Windows user.");
    }
    std::wstring result(sid);
    LocalFree(sid);
    return result;
}
} // namespace

std::wstring wide(const std::string &text) {
    if (text.empty()) {
        return {};
    }
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (!size) {
        throw InputError("Input contains invalid UTF-8.");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}

std::string utf8(const std::wstring &text) {
    if (text.empty()) {
        return {};
    }
    const auto size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) {
        throw InputError("A Windows path contains invalid Unicode.");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::string native_filename(const std::filesystem::path &path) {
    // PJLIB's Windows file API uses the ANSI code page. Inputs are read through
    // wide Windows paths; only our canonical snapshot needs this conversion.
    const auto &value = path.native();
    BOOL replaced = FALSE;
    const auto size =
        WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.data(),
                            static_cast<int>(value.size()), nullptr, 0, nullptr, &replaced);
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!size || replaced ||
        !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.data(),
                             static_cast<int>(value.size()), result.data(), size, nullptr,
                             &replaced) ||
        replaced) {
        throw InputError("The snapshot folder is not representable by PJLIB's Windows file API.");
    }
    return result;
}

std::string unique_id() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) {
        throw std::runtime_error("Cannot generate a request identifier.");
    }
    wchar_t value[40]{};
    StringFromGUID2(id, value, 40);
    return utf8(std::wstring(value + 1, 36));
}

std::filesystem::path private_root() {
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!size) {
        throw std::runtime_error("LOCALAPPDATA is unavailable.");
    }
    std::wstring base(size, L'\0');
    const auto written = GetEnvironmentVariableW(L"LOCALAPPDATA", base.data(), size);
    if (!written || written >= size) {
        throw std::runtime_error("Cannot locate local application data.");
    }
    base.resize(written);
    const auto root = std::filesystem::path(base) / L"AIPhone";
    const auto descriptor = L"D:P(A;;FA;;;SY)(A;;FA;;;" + user_sid() + L")";
    PSECURITY_DESCRIPTOR security = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1,
                                                              &security, nullptr)) {
        throw std::runtime_error("Cannot prepare private audio permissions.");
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security, FALSE};
    const auto created = CreateDirectoryW(root.c_str(), &attributes);
    const auto error = GetLastError();
    LocalFree(security);
    if (!created && error != ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("Cannot create private audio folder.");
    }
    const auto flags = GetFileAttributesW(root.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) ||
        (flags & FILE_ATTRIBUTE_REPARSE_POINT)) {
        throw std::runtime_error("Private audio folder must be a real local directory.");
    }
    return root;
}

std::filesystem::path local_file(const std::filesystem::path &path) {
    const auto supplied = path.native();
    if (supplied.rfind(L"\\\\", 0) == 0 || supplied.rfind(L"//", 0) == 0 ||
        (path.has_root_path() && GetDriveTypeW(path.root_path().c_str()) == DRIVE_REMOTE)) {
        throw InputError("Network and device file paths are unsupported.");
    }
    try {
        const auto absolute = std::filesystem::canonical(path);
        const auto text = absolute.native();
        if (text.rfind(L"\\\\", 0) == 0 || !std::filesystem::is_regular_file(absolute) ||
            GetDriveTypeW(absolute.root_path().c_str()) == DRIVE_REMOTE) {
            throw InputError(
                "Input must be a regular local file; network and device paths are unsupported.");
        }
        return absolute;
    } catch (const std::filesystem::filesystem_error &) {
        throw InputError("The requested local file does not exist or is inaccessible.");
    }
}

NetworkRuntime::NetworkRuntime() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("Windows networking initialization failed.");
    }
}
NetworkRuntime::~NetworkRuntime() {
    WSACleanup();
}

void check_local_ports(const Config &config) {
    std::vector<SOCKET> sockets;
    try {
        for (auto port : {config.local_sip_port, config.rtp_port, config.rtcp_port}) {
            const auto socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (socket_handle == INVALID_SOCKET) {
                throw std::runtime_error("Cannot create a diagnostic UDP socket.");
            }
            sockets.push_back(socket_handle);
            const BOOL exclusive = TRUE;
            setsockopt(socket_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                       reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<unsigned short>(port));
            InetPtonA(AF_INET, config.local_ip.c_str(), &address.sin_addr);
            if (bind(socket_handle, reinterpret_cast<const sockaddr *>(&address),
                     sizeof(address)) != 0) {
                throw InputError("The selected LAN address is not assigned locally, or UDP port " +
                                 std::to_string(port) + " is already in use.");
            }
        }
    } catch (...) {
        for (const auto item : sockets) {
            closesocket(item);
        }
        throw;
    }
    for (const auto item : sockets) {
        closesocket(item);
    }
}

ConsoleCancellation::ConsoleCancellation() {
    InterlockedExchange(&cancelled, 0);
    if (!SetConsoleCtrlHandler(console_handler, TRUE)) {
        throw std::runtime_error("Cannot install the console cancellation handler.");
    }
}
ConsoleCancellation::~ConsoleCancellation() {
    SetConsoleCtrlHandler(console_handler, FALSE);
}
bool cancellation_requested() {
    return InterlockedCompareExchange(&cancelled, 0, 0) != 0;
}

SingleInstance::SingleInstance(const std::string &phone_ip) {
    const auto name = L"Local\\AIPhone." + user_sid() + L"." + wide(phone_ip);
    mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!mutex_) {
        throw std::runtime_error("Cannot create the call mutex.");
    }
    const auto status = WaitForSingleObject(mutex_, 0);
    acquired_ = status == WAIT_OBJECT_0 || status == WAIT_ABANDONED;
    if (!acquired_ && status != WAIT_TIMEOUT) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        throw std::runtime_error("Cannot acquire the call mutex.");
    }
}
SingleInstance::~SingleInstance() {
    if (acquired_) {
        ReleaseMutex(mutex_);
    }
    if (mutex_) {
        CloseHandle(mutex_);
    }
}
} // namespace aiphone
