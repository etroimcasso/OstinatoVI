#include <iostream>

#include "ostinato/ostinato.h"
#include "retropp/version.h"

// Entry stub: proves the port binary links the engine and runs consumer-side.
// No window, no run loop yet — the windowed run loop lands with the first
// windowed feature.
int main() {
    std::cout << ostinato::portName() << " on Retro++ " << retropp::version() << '\n';
    return 0;
}
