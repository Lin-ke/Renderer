#include "engine/core/window/Window.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/render_system.h"

int main() {
    Log::init();
    INFO("Logger initialized successfully!");

    Window window(800, 600, L"Renderer Window");
    
    RenderSystem render_system;
    render_system.initialize(window.get_hwnd());

    while (window.process_messages()) {
        render_system.tick();
    }

    return 0;
}
