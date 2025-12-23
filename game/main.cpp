#include "engine/core/window/Window.h"
#include "engine/core/log/Log.h"

int main() {
    Engine::Log::init();
    LOG("Logger initialized successfully!");
    WARN("This is a warning test.");

    Engine::Window window(800, 600, L"Renderer Window");

    while (window.process_messages()) {
        Sleep(10);
    }

    return 0;
}
