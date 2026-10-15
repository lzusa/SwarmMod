#include "../src/State.hpp"
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
    std::cout << "state transitions and JSON escaping passed\n";
}
