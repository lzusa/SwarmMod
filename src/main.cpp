#include <winsock2.h>
#include <windows.h>
#include <winhttp.h> // Read user proxy settings only; requests use libcurl/mbedTLS.
#include <wincrypt.h>
#include <curl/curl.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include "Publisher.hpp"
#include <SokuLib.hpp>
#include <mutex>
#include <string>
#include "State.hpp"

namespace {
std::mutex stateMutex;
swarm::State state;
std::string profileName = "Swarm";
DWORD lastFrame = 0;
DWORD lastName = 0;
std::wstring configPath;
std::wstring resultPath;
std::wstring debugPath;

void debugLog(const std::string &message) {
#ifdef _DEBUG
    if (debugPath.empty()) return;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(debugPath.c_str(), GetFileExInfoStandard, &info) &&
        (info.nFileSizeHigh || info.nFileSizeLow > 1024 * 1024))
        MoveFileExW(debugPath.c_str(), (debugPath + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    HANDLE file = CreateFileW(debugPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now{}; GetLocalTime(&now);
    char timestamp[96];
    sprintf_s(timestamp, "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu ", now.wYear, now.wMonth,
        now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentProcessId());
    std::string line = timestamp + message + "\r\n";
    DWORD written; WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
#else
    (void)message;
#endif
}
DWORD requestFailed(const char *stage) {
    const DWORD error = GetLastError();
    debugLog(std::string(stage) + " failed, Win32Error=" + std::to_string(error));
    return 0;
}

std::string toUtf8(const std::string &text) {
    if (text.empty()) return "Swarm";
    int count = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
    count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), count, nullptr, nullptr);
    return out;
}

void observe(swarm::Role role, swarm::Phase phase) {
    if (SokuLib::subMode == SokuLib::BATTLE_SUBMODE_REPLAY) {
        role = swarm::Role::None;
        phase = swarm::Phase::Idle;
    }
    const DWORD now = GetTickCount();
    std::lock_guard<std::mutex> lock(stateMutex);
    state.observe(role, phase);
    lastFrame = now;
    if (role == swarm::Role::Host && now - lastName >= 1000) {
        profileName = toUtf8(SokuLib::profile1.name.operator std::string());
        lastName = now;
    }
}

// Chain the previous callback so SokuLobbies / rollback mods keep their hooks.
#define SCENE_HOOK(Type, RoleValue, PhaseValue) \
    int (SokuLib::Type::*original##Type)(); \
    int __fastcall process##Type(SokuLib::Type *self, void *) { \
        observe(swarm::Role::RoleValue, swarm::Phase::PhaseValue); \
        return (self->*original##Type)(); \
    }
SCENE_HOOK(Logo, None, Idle)
SCENE_HOOK(Title, None, Idle)
SCENE_HOOK(Select, None, Idle)
SCENE_HOOK(Battle, None, Idle)
SCENE_HOOK(SelectServer, Host, Select)
SCENE_HOOK(LoadingServer, Host, Loading)
SCENE_HOOK(BattleServer, Host, Battle)
SCENE_HOOK(SelectClient, Client, Select)
SCENE_HOOK(LoadingClient, Client, Loading)
SCENE_HOOK(BattleClient, Client, Battle)
SCENE_HOOK(LoadingWatch, Spectator, Loading)
SCENE_HOOK(BattleWatch, Spectator, Battle)


std::wstring readSetting(const wchar_t *key) {
    wchar_t value[2048]{};
    GetPrivateProfileStringW(L"AutoPush", key, L"", value, 2048, configPath.c_str());
    return value;
}
std::string utf8(const std::wstring &value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}
bool sessionAlive(const std::wstring &name) {
    if (name.rfind(L"SwarmModRelay-", 0) != 0) return false;
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
    if (!mutex) return false;
    CloseHandle(mutex);
    return true;
}
struct Settings {
    std::wstring address, endpoint, session;
    bool enabled = false;
    bool operator==(const Settings &) const = default;
};
Settings settings() {
    // The address is written last by Swarm, after ownership and switch state.
    return {readSetting(L"RelayAddress"), readSetting(L"Endpoint"), readSetting(L"Session"),
        GetPrivateProfileIntW(L"AutoPush", L"Enabled", 0, configPath.c_str()) == 1};
}

// Convert Windows trusted root certificates to an in-memory PEM bundle.
// No external CA file, TLS verification bypass, or helper executable is needed.
std::string windowsRoots() {
    std::string pem;
    for (DWORD location : {CERT_SYSTEM_STORE_CURRENT_USER, CERT_SYSTEM_STORE_LOCAL_MACHINE}) {
        HCERTSTORE store=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,
            location|CERT_STORE_OPEN_EXISTING_FLAG|CERT_STORE_READONLY_FLAG,L"ROOT");
        if(!store) continue;
        PCCERT_CONTEXT cert=nullptr;
        while((cert=CertEnumCertificatesInStore(store,cert))!=nullptr) {
            DWORD size=0;
            if(!CryptBinaryToStringA(cert->pbCertEncoded,cert->cbCertEncoded,
                CRYPT_STRING_BASE64HEADER,nullptr,&size)) continue;
            std::string encoded(size,'\0');
            if(CryptBinaryToStringA(cert->pbCertEncoded,cert->cbCertEncoded,
                CRYPT_STRING_BASE64HEADER,encoded.data(),&size)) {
                if(!encoded.empty() && encoded.back()=='\0') encoded.pop_back();
                pem+=encoded;
            }
        }
        CertCloseStore(store,0);
    }
    return pem;
}
struct ProxySettings { std::string proxy,bypass; };
ProxySettings userProxy(const std::wstring &endpoint) {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
    ProxySettings out;
    if(WinHttpGetIEProxyConfigForCurrentUser(&config)) {
        if(config.lpszProxy) {
            const std::wstring specification=config.lpszProxy;
            if(specification.find(L'=')==std::wstring::npos) out.proxy=utf8(specification);
            else {
                const std::wstring scheme=endpoint.rfind(L"https://",0)==0 ? L"https=" : L"http=";
                size_t offset=0;
                while(offset<specification.size()) {
                    const auto end=specification.find(L';',offset);
                    const auto item=specification.substr(offset,end==std::wstring::npos ? end : end-offset);
                    if(item.rfind(scheme,0)==0) out.proxy=utf8(item.substr(scheme.size()));
                    if(end==std::wstring::npos) break;
                    offset=end+1;
                }
            }
        }
        if(config.lpszProxyBypass) {
            out.bypass=utf8(config.lpszProxyBypass);
            std::replace(out.bypass.begin(),out.bypass.end(),';',',');
        }
    }
    if(config.lpszProxy) LocalFree(config.lpszProxy);
    if(config.lpszProxyBypass) LocalFree(config.lpszProxyBypass);
    if(config.lpszAutoConfigUrl) LocalFree(config.lpszAutoConfigUrl);
    return out;
}
size_t receiveBody(char *data,size_t size,size_t count,void *context) {
    try {
        const size_t bytes=size*count;
        auto &response=*static_cast<std::string *>(context);
        response.append(data,(std::min)(bytes,4096-response.size()));
        return bytes;
    } catch(...) { return 0; }
}
int cancelTransfer(void *context,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    try {
        const auto &original=*static_cast<const Settings *>(context);
        return !(settings()==original) || !sessionAlive(original.session);
    } catch(...) { return 1; }
}
DWORD publish(const Settings &config,const std::string &profile) {
    if(!config.enabled || !(settings()==config) || !sessionAlive(config.session)) {
        debugLog("request skipped: disabled, stale config or expired session"); return 0;
    }
    std::string host;
    unsigned port=0;
    if(!swarm::relayParts(utf8(config.address),host,port)) {
        debugLog("request skipped: invalid relay address"); return 0;
    }
    static const CURLcode initialized=curl_global_init(CURL_GLOBAL_DEFAULT);
    if(initialized!=CURLE_OK) {debugLog("curl_global_init failed"); return 0;}
    auto request=std::unique_ptr<CURL,decltype(&curl_easy_cleanup)>(curl_easy_init(),curl_easy_cleanup);
    if(!request) { debugLog("curl_easy_init failed"); return 0; }
    const auto endpoint=utf8(config.endpoint);
    const auto proxy=userProxy(config.endpoint);
    const std::string body="{\"host\":"+swarm::jsonString(host)+",\"port\":"+std::to_string(port)+
        ",\"profile_name\":"+swarm::jsonString(profile)+",\"message\":\"Swarm\"}";
    auto headers=std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)>(
        curl_slist_append(nullptr,"Content-Type: application/json"),curl_slist_free_all);
    if(!headers) return 0;
    std::string response;
    char error[CURL_ERROR_SIZE]{};
    CURLcode optionError=CURLE_OK;
    const auto set=[&](CURLoption option,auto value) {
        const auto result=curl_easy_setopt(request.get(),option,value);
        if(result!=CURLE_OK) optionError=result;
    };
    set(CURLOPT_URL,endpoint.c_str());
    set(CURLOPT_PROTOCOLS_STR,"http,https");
    set(CURLOPT_CUSTOMREQUEST,"PUT");
    set(CURLOPT_HTTPHEADER,headers.get());
    set(CURLOPT_POSTFIELDS,body.data());
    set(CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(body.size()));
    set(CURLOPT_USERAGENT,"SwarmMod/0.1.0");
    set(CURLOPT_CONNECTTIMEOUT_MS,15000L);
    set(CURLOPT_TIMEOUT_MS,45000L);
    set(CURLOPT_NOSIGNAL,1L);
    set(CURLOPT_FOLLOWLOCATION,0L);
    set(CURLOPT_IPRESOLVE,static_cast<long>(CURL_IPRESOLVE_V4));
    set(CURLOPT_WRITEFUNCTION,receiveBody);
    set(CURLOPT_WRITEDATA,&response);
    set(CURLOPT_ERRORBUFFER,error);
    set(CURLOPT_NOPROGRESS,0L);
    set(CURLOPT_XFERINFOFUNCTION,cancelTransfer);
    set(CURLOPT_XFERINFODATA,&config);
    set(CURLOPT_SSL_VERIFYPEER,1L);
    set(CURLOPT_SSL_VERIFYHOST,2L);
    if(!proxy.proxy.empty()) {
        set(CURLOPT_PROXY,proxy.proxy.c_str());
        const auto bypass=proxy.bypass+",localhost,127.0.0.1,::1";
        set(CURLOPT_NOPROXY,bypass.c_str());
    }
    if(config.endpoint.rfind(L"https://",0)==0) {
        static const std::string trust=windowsRoots();
        if(trust.empty()) { debugLog("no Windows trusted roots; HTTPS request not sent"); return 0; }
        curl_blob roots{const_cast<char *>(trust.data()),trust.size(),CURL_BLOB_COPY};
        set(CURLOPT_CAINFO_BLOB,&roots);
    }
    if(optionError!=CURLE_OK) {
        debugLog(std::string("curl option failed: ")+curl_easy_strerror(optionError)); return 0;
    }
    debugLog(std::string("transport=")+curl_version()+" proxy="+(proxy.proxy.empty() ? "environment/default" : "system/manual"));
    debugLog("prepare PUT endpoint="+endpoint+" relay="+utf8(config.address));
    debugLog("send body="+body);
    const auto result=curl_easy_perform(request.get());
    long status=0;
    curl_easy_getinfo(request.get(),CURLINFO_RESPONSE_CODE,&status);
    debugLog("curl result="+std::to_string(result)+" detail="+(error[0] ? error : curl_easy_strerror(result))+
        " HTTP status="+std::to_string(status)+" response="+response);
    return result==CURLE_OK ? static_cast<DWORD>(status) : 0;
}
DWORD WINAPI publisher(void *) {
    swarm::PublishSchedule schedule;
    std::string lastObservation;
    debugLog("publisher worker started");
    for (;;) {
        try {
            swarm::State snapshot;
            std::string profile;
            DWORD frame;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                snapshot = state; profile = profileName; frame = lastFrame;
            }
            const auto config = settings();
            std::string host;
            unsigned port = 0;
            const bool alive = sessionAlive(config.session);
            const DWORD sessionError = alive ? 0 : GetLastError();
            const bool responsive = frame && GetTickCount() - frame < 10000;
            const bool addressValid = swarm::relayParts(utf8(config.address), host, port);
            const std::string observation = "enabled=" + std::to_string(config.enabled) +
                " role=" + swarm::name(snapshot.role) + " phase=" + swarm::name(snapshot.phase) +
                " connection=" + std::to_string(snapshot.connection) + " responsive=" + std::to_string(responsive) +
                " relay=" + utf8(config.address) + " valid=" + std::to_string(addressValid) +
                " endpoint=" + utf8(config.endpoint) + " session=" + utf8(config.session) +
                " sessionAlive=" + std::to_string(alive) + " sessionError=" + std::to_string(sessionError);
            if (observation != lastObservation) { debugLog(observation); lastObservation = observation; }
            if (config.enabled && alive && responsive &&
                snapshot.role == swarm::Role::Host && snapshot.phase == swarm::Phase::Battle &&
                swarm::relayParts(utf8(config.address), host, port)) {
                const std::string key = std::to_string(snapshot.connection) + "|" + utf8(config.address) + "|" +
                    utf8(config.session) + "|" + utf8(config.endpoint);
                if (schedule.due(key, GetTickCount64())) {
                    debugLog("attempt=" + std::to_string(schedule.attempts + 1) + " key=" + key);
                    DWORD code = publish(config, profile);
                    schedule.result(code >= 200 && code < 300, GetTickCount64());
                    debugLog("attempt complete, success=" + std::to_string(schedule.complete) +
                        " attempts=" + std::to_string(schedule.attempts) + " nextTick=" + std::to_string(schedule.next));
                    WritePrivateProfileStringW(L"LastPush", L"Status", std::to_wstring(code).c_str(), resultPath.c_str());
                    WritePrivateProfileStringW(L"LastPush", L"Success", code >= 200 && code < 300 ? L"1" : L"0", resultPath.c_str());
                }
            }
        } catch (...) { debugLog("publisher caught an exception"); }
        Sleep(500);
    }
}

}

extern "C" __declspec(dllexport) bool CheckVersion(const BYTE hash[16]) {
    return memcmp(hash, SokuLib::targetHash, 16) == 0;
}
extern "C" __declspec(dllexport) bool Initialize(HMODULE module, HMODULE) {
    wchar_t filename[MAX_PATH]{};
    if (!GetModuleFileNameW(module, filename, MAX_PATH)) return false;
    std::wstring directory(filename);
    directory.resize(directory.find_last_of(L"\\/") + 1);
    configPath = directory + L"SwarmMod.ini";
    resultPath = directory + L"SwarmMod-result.ini";
    debugPath = directory + L"SwarmMod-debug.log";
    debugLog("SwarmMod 0.1.0 DEBUG initialization; config=" + utf8(configPath));
    if (GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // CREATE_NEW cannot overwrite a config created concurrently by Swarm.
        HANDLE config = CreateFileW(configPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr);
        if (config != INVALID_HANDLE_VALUE) {
            const char defaults[] = "[AutoPush]\r\nEnabled=0\r\nRelayAddress=\r\nEndpoint=https://konni.delthas.fr/games\r\nSession=\r\n";
            DWORD written;
            WriteFile(config, defaults, sizeof(defaults) - 1, &written, nullptr);
            CloseHandle(config);
        }
    }
    // SWRSToys modules live for the process lifetime; pin before creating worker.
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(module), &pinned)) { requestFailed("GetModuleHandleExW"); return false; }
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void *>(RDATA_SECTION_OFFSET), RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old)) { requestFailed("VirtualProtect"); return false; }
#define INSTALL(Type) original##Type = SokuLib::TamperDword(&SokuLib::VTable_##Type.onProcess, process##Type)
    INSTALL(Logo); INSTALL(Title); INSTALL(Select); INSTALL(Battle);
    INSTALL(SelectServer); INSTALL(LoadingServer); INSTALL(BattleServer);
    INSTALL(SelectClient); INSTALL(LoadingClient); INSTALL(BattleClient);
    INSTALL(LoadingWatch); INSTALL(BattleWatch);
    VirtualProtect(reinterpret_cast<void *>(RDATA_SECTION_OFFSET), RDATA_SECTION_SIZE, old, &old);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    debugLog("scene hooks installed");
    HANDLE thread = CreateThread(nullptr, 0, publisher, nullptr, 0, nullptr);
    if (!thread) { requestFailed("CreateThread"); return false; }
    CloseHandle(thread);
    return true;
}
extern "C" __declspec(dllexport) int getPriority() { return -60000; }
BOOL WINAPI DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
