#pragma once
#include <cstdint>
#include <string>
#include <charconv>
#include "State.hpp"

namespace swarm {
inline bool relayParts(const std::string &address, std::string &host, unsigned &port) {
    const auto colon = address.find(':');
    if (colon == std::string::npos || address.find(':', colon + 1) != std::string::npos) return false;
    host = address.substr(0, colon);
    const auto text = address.substr(colon + 1);
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (ec != std::errc{} || end != text.data() + text.size() || port == 0 || port > 65535) return false;
    unsigned octets = 0;
    size_t start = 0;
    while (start < host.size()) {
        auto dot = host.find('.', start);
        auto stop = dot == std::string::npos ? host.size() : dot;
        unsigned value = 0;
        auto [last, error] = std::from_chars(host.data() + start, host.data() + stop, value);
        if (error != std::errc{} || last != host.data() + stop || value > 255) return false;
        ++octets;
        if (dot == std::string::npos) break;
        start = dot + 1;
        if (start == host.size()) return false;
    }
    return octets == 4;
}
struct PublishSchedule {
    std::string key;
    unsigned attempts = 0;
    uint64_t next = 0;
    bool complete = false;
    bool due(const std::string &value, uint64_t now) {
        if (key != value) { key = value; attempts = 0; next = 0; complete = false; }
        return !complete && attempts < 3 && now >= next;
    }
    void result(bool success, uint64_t now) {
        complete = success;
        ++attempts;
        next = now + (attempts == 1 ? 15000 : 60000);
    }
};
}
