#include <cstdio>

#include "check.hpp"

int main() {
    int broken = 0;
    for (const check::Case& each : check::cases()) {
        int before = check::failures();
        try {
            each.run();
        } catch (const ffrwd::Error&) {
        }
        if (check::failures() != before) {
            ++broken;
            std::fprintf(stderr, "FAIL %s\n", each.name);
        }
    }
    std::printf("%zu tests, %d failed\n", check::cases().size(), broken);
    return broken == 0 ? 0 : 1;
}
