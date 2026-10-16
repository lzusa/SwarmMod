#include "../src/State.hpp"
#include "../src/Publisher.hpp"
#include <cstdlib>
#include <iostream>
void check(bool condition) { if (!condition) std::abort(); }
int main() {
    using namespace swarm;
    State s;
    s.observe(Role::Host, Phase::Select);
    check(s.connection == 1 && !s.battleSeen);
    s.observe(Role::Host, Phase::Battle);
    check(s.connection == 1 && s.battleSeen);
    s.observe(Role::Host, Phase::Select);
    s.observe(Role::Host, Phase::Loading);
    s.observe(Role::Host, Phase::Battle);
    check(s.connection == 1); // rematches must not repost
    s.observe(Role::None, Phase::Idle);
    check(!s.battleSeen);
    s.observe(Role::Host, Phase::Battle);
    check(s.connection == 2 && s.battleSeen);
    s.observe(Role::Client, Phase::Battle);
    check(s.connection == 3 && s.role == Role::Client);
    s.observe(Role::Spectator, Phase::Battle);
    check(s.role == Role::Spectator);
    check(jsonString("a\n\"\\") == "\"a\\u000a\\\"\\\\\"");
    std::string host;
    unsigned port;
    check(relayParts("203.0.113.10:40681", host, port) && host == "203.0.113.10" && port == 40681);
    for (auto invalid : {"1.2.3.4:0", "1.2.3.4:65536", "1.2.3.999:42", "1.2.3.:42", "host:42", "1.2.3.4:42x", "1.2.3.4:-1"})
        check(!relayParts(invalid, host, port));
    PublishSchedule schedule;
    check(schedule.due("connection-1", 0));
    schedule.result(false, 0);
    check(!schedule.due("connection-1", 14999) && schedule.due("connection-1", 15000));
    schedule.result(false, 15000);
    check(!schedule.due("connection-1", 74999) && schedule.due("connection-1", 75000));
    schedule.result(false, 75000);
    check(!schedule.due("connection-1", 999999));
    check(schedule.due("connection-2", 999999));
    schedule.result(true, 999999);
    check(!schedule.due("connection-2", 1999999));
    check(schedule.due("connection-2-new-relay", 1999999));
    std::cout << "state transitions and JSON escaping passed\n";
}
