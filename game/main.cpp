#include "engine/main/engine_context.h"
#include <bitset>
int main() {
    EngineContext::init(-1);
    // load scene.
    EngineContext::main_loop();
    EngineContext::exit();
    return 0;
}