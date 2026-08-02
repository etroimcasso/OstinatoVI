#include <iostream>

#include "ostinato/ostinato.h"
#include "retropp/version.h"

// Phase-0 entry stub: prove the port binary links the engine and runs consumer-side.
// No window, no run loop — the first windowed feature belongs to a later phase.
int main() {
    std::cout << ostinato::portName() << " on Retro++ " << retropp::version() << '\n';
    return 0;
}
