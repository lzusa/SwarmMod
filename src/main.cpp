#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>
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
std::wstring statusPath;
std::string instance;

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

DWORD WINAPI heartbeat(void *) {
    uint64_t sequence = 0;
    for (;;) {
        swarm::State snapshot;
        std::string profile;
        DWORD frame;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            snapshot = state;
            profile = profileName;
            frame = lastFrame;
        }
        // A worker alone is not evidence of an updating game loop.
        const bool responsive = frame && GetTickCount() - frame < 10000;
        const std::string json = "{\"protocol\":1,\"version\":\"0.1.0\",\"pid\":" +
            std::to_string(GetCurrentProcessId()) + ",\"instance\":" + swarm::jsonString(instance) +
            ",\"heartbeat\":" + std::to_string(++sequence) + ",\"responsive\":" + (responsive ? "true" : "false") +
            ",\"connection\":" + std::to_string(snapshot.connection) + ",\"role\":" + swarm::jsonString(swarm::name(snapshot.role)) +
            ",\"phase\":" + swarm::jsonString(swarm::name(snapshot.phase)) + ",\"profile_name\":" + swarm::jsonString(profile) + "}";
        const auto temporary = statusPath + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const bool ok = WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) && written == json.size();
            CloseHandle(file);
            if (ok) MoveFileExW(temporary.c_str(), statusPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
        Sleep(500);
    }
}
}

extern "C" __declspec(dllexport) bool CheckVersion(const BYTE hash[16]) {
    return memcmp(hash, SokuLib::targetHash, 16) == 0;
}
extern "C" __declspec(dllexport) bool Initialize(HMODULE module, HMODULE) {
    wchar_t folder[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, folder))) return false;
    std::wstring directory = std::wstring(folder) + L"\\swarmmod";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    statusPath = directory + L"\\status.json";
    instance = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    // SWRSToys modules live for the process lifetime; pin before creating worker.
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(module), &pinned)) return false;
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void *>(RDATA_SECTION_OFFSET), RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old)) return false;
#define INSTALL(Type) original##Type = SokuLib::TamperDword(&SokuLib::VTable_##Type.onProcess, process##Type)
    INSTALL(Logo); INSTALL(Title); INSTALL(Select); INSTALL(Battle);
    INSTALL(SelectServer); INSTALL(LoadingServer); INSTALL(BattleServer);
    INSTALL(SelectClient); INSTALL(LoadingClient); INSTALL(BattleClient);
    INSTALL(LoadingWatch); INSTALL(BattleWatch);
    VirtualProtect(reinterpret_cast<void *>(RDATA_SECTION_OFFSET), RDATA_SECTION_SIZE, old, &old);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    HANDLE thread = CreateThread(nullptr, 0, heartbeat, nullptr, 0, nullptr);
    if (!thread) return false;
    CloseHandle(thread);
    return true;
}
extern "C" __declspec(dllexport) int getPriority() { return -60000; }
BOOL WINAPI DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
