#include "engine/main/engine_context.h"
int main() {
    EngineContext::init();
    // load scene.
    EngineContext::main_loop();
    EngineContext::exit();
    return 0;
}
