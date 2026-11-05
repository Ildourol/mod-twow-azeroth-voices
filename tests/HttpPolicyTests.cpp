// Engine-free policy regression test:
// g++ -std=c++17 -Wall -Wextra -Werror tests/HttpPolicyTests.cpp -o /tmp/av-http-policy && /tmp/av-http-policy
#include "../src/AzerothVoicesHttpPolicy.h"
#include <cstdlib>
#include <iostream>

int main()
{
    unsigned checks = 0;
    auto check = [&checks](bool condition) {
        ++checks;
        if (!condition)
        {
            std::cerr << "HTTP policy check failed: " << checks << '\n';
            std::exit(EXIT_FAILURE);
        }
    };
    using AzerothVoices::IsAllowedHttpHost;
    check(!IsAllowedHttpHost("ollama", ""));
    check(IsAllowedHttpHost("ollama", "ollama"));
    check(IsAllowedHttpHost("OLLAMA", " other, Ollama ,host.docker.internal "));
    check(IsAllowedHttpHost("host.docker.internal", "ollama,host.docker.internal"));
    check(IsAllowedHttpHost("192.168.1.20", "192.168.1.20"));
    check(IsAllowedHttpHost("[fd00::20]", "[fd00::20]"));
    check(!IsAllowedHttpHost("ollama.attacker.example", "ollama"));
    check(!IsAllowedHttpHost("attackerollama", "ollama"));
    check(!IsAllowedHttpHost("ollama", "http://ollama:11434/v1/chat/completions"));
    check(!IsAllowedHttpHost("ollama", "ollama:11434"));
    check(!IsAllowedHttpHost("ollama", "*"));
    check(!IsAllowedHttpHost("", ", ,"));
    check(!IsAllowedHttpHost("example.com", "ollama,host.docker.internal,192.168.1.20"));
    std::cout << checks << " HTTP policy checks passed\n";
}
