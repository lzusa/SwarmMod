// Exercise the actual libcurl/mbedTLS publisher against a local test server.
#include "../src/main.cpp"
#include <filesystem>
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const auto folder = std::filesystem::temp_directory_path() / (L"SwarmMod http test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    configPath = (folder / L"SwarmMod.ini").wstring();
    HANDLE owner = CreateMutexW(nullptr, FALSE, L"SwarmModRelay-http-test");
    std::wstring endpoint;
    for (const char *p = argv[1]; *p; ++p) endpoint += static_cast<wchar_t>(*p);
    WritePrivateProfileStringW(L"AutoPush", L"Enabled", L"1", configPath.c_str());
    WritePrivateProfileStringW(L"AutoPush", L"Session", L"SwarmModRelay-http-test", configPath.c_str());
    WritePrivateProfileStringW(L"AutoPush", L"RelayAddress", L"203.0.113.10:40681", configPath.c_str());
    WritePrivateProfileStringW(L"AutoPush", L"Endpoint", endpoint.c_str(), configPath.c_str());
    if (publish(settings(), "test\"player") != 200) return 3;
    if (publish(settings(), "test\"player-测试") != 200) return 7;
    auto previous = settings();
    WritePrivateProfileStringW(L"AutoPush", L"Enabled", L"0", configPath.c_str());
    if (publish(previous, "cancelled") != 0) return 4;
    CloseHandle(owner);
    if (sessionAlive(L"SwarmModRelay-http-test")) return 5;
    if (publish(settings(), "no-owner") != 0) return 6;
    std::filesystem::remove_all(folder);
    std::cout << "libcurl/mbedTLS PUT, JSON escaping, config cancellation and expired session passed\n";
}
