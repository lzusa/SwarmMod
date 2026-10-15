#pragma once
#include <cstdint>
#include <string>

namespace swarm {
enum class Phase { Idle, Select, Loading, Battle };
enum class Role { None, Host, Client, Spectator };

// A connection spans battle -> character select -> battle rematches.
struct State {
    Phase phase = Phase::Idle;
    Role role = Role::None;
    uint64_t connection = 0;
    bool battleSeen = false;

    void observe(Role nextRole, Phase nextPhase) {
        if (nextRole == Role::None) {
            role = Role::None;
            phase = Phase::Idle;
            battleSeen = false;
            return;
        }
        if (role != nextRole) {
            ++connection;
            battleSeen = false;
        }
        role = nextRole;
        phase = nextPhase;
        if (phase == Phase::Battle) battleSeen = true;
    }
};
inline const char *name(Role role) {
    switch (role) {
    case Role::Host: return "host";
    case Role::Client: return "client";
    case Role::Spectator: return "spectator";
    default: return "none";
    }
}
inline const char *name(Phase phase) {
    switch (phase) {
    case Phase::Select: return "select";
    case Phase::Loading: return "loading";
    case Phase::Battle: return "battle";
    default: return "idle";
    }
}
inline std::string jsonString(const std::string &value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 0x20) {
            out += "\\u00";
            out += hex[c >> 4]; out += hex[c & 15];
        } else out += c;
    }
    return out + '"';
}
}
