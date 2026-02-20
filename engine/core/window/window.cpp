#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Window.h"
#include "engine/function/input/input.h"
#include <windowsx.h> // For GET_X_LPARAM, GET_Y_LPARAM
#include <imgui.h>
#include <imgui_impl_win32.h>

// Forward declaration of ImGui Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


Window::Window(int width, int height, const std::wstring& title, bool visible) 
    : width_(width), height_(height), title_(title), visible_(visible), 
      hinstance_(GetModuleHandle(nullptr)), class_name_(L"RendererWindowClass")
{
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hinstance_;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = class_name_.c_str();
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    RegisterClassEx(&wc);

    DWORD style = visible ? WS_OVERLAPPEDWINDOW : 0;
    
    hwnd_ = CreateWindowEx(
        0,
        class_name_.c_str(),
        title_.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width_, height_,
        nullptr,
        nullptr,
        hinstance_,
        this
    );

    if (!hwnd_) {
        DWORD error = GetLastError();
        // Log error or handle it
        (void)error;
    }

    if (hwnd_ && visible_) {
        ShowWindow(hwnd_, SW_SHOW);
    }
}

Window::~Window() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
    UnregisterClass(class_name_.c_str(), hinstance_);
}

bool Window::process_messages() {
    MSG msg = { 0 };
    // Handle all pending messages
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

LRESULT CALLBACK Window::window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Forward to ImGui first (only if ImGui is initialized)
    // NOTE: ImGui_ImplWin32_WndProcHandler returns true if it handled the message
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        // Don't return here - let input system also process the message
    }
    
    // Check if ImGui wants to capture input (only if ImGui is initialized)
    bool imgui_wants_mouse = false;
    bool imgui_wants_keyboard = false;
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        imgui_wants_mouse = io.WantCaptureMouse;
        imgui_wants_keyboard = io.WantCaptureKeyboard;
    }
    
    auto& input = Input::get_instance();

    switch (msg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            input.on_key_down(static_cast<Key>(wParam));
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            input.on_key_up(static_cast<Key>(wParam));
            return 0;

        case WM_MOUSEMOVE:
            // Always update mouse position for input system
            input.on_mouse_move(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_LBUTTONDOWN:
            input.on_mouse_button_down(MouseButton::Left);
            return 0;

        case WM_LBUTTONUP:
            input.on_mouse_button_up(MouseButton::Left);
            return 0;

        case WM_RBUTTONDOWN:
            input.on_mouse_button_down(MouseButton::Right);
            return 0;

        case WM_RBUTTONUP:
            input.on_mouse_button_up(MouseButton::Right);
            return 0;
        
        case WM_MBUTTONDOWN:
            input.on_mouse_button_down(MouseButton::Middle);
            return 0;

        case WM_MBUTTONUP:
            input.on_mouse_button_up(MouseButton::Middle);
            return 0;
        
        case WM_MOUSEWHEEL:
            // Mouse wheel not implemented in Input class yet
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
