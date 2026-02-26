#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Window.h"
#include "engine/function/input/input.h"
#include <windowsx.h> // For GET_X_LPARAM, GET_Y_LPARAM
#include <imgui.h>
#include <imgui_impl_win32.h>


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


static int s_window_class_counter = 0;

Window::Window(int width, int height, const std::wstring& title, bool visible) 
    : width_(width), height_(height), title_(title), visible_(visible), 
      hinstance_(GetModuleHandle(nullptr)), 
      class_name_(L"RendererWindowClass_" + std::to_wstring(++s_window_class_counter))
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
    
    // Calculate required window size to ensure client area matches width/height
    RECT window_rect = { 0, 0, width_, height_ };
    AdjustWindowRect(&window_rect, style, FALSE);
    int adjusted_width = window_rect.right - window_rect.left;
    int adjusted_height = window_rect.bottom - window_rect.top;

    hwnd_ = CreateWindowEx(
        0,
        class_name_.c_str(),
        title_.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        adjusted_width, adjusted_height,
        nullptr,
        nullptr,
        hinstance_,
        this
    );

    if (!hwnd_) {
        DWORD error = GetLastError();
        //####TODO####: Add proper error handling/logging for window creation failure
        (void)error;
    }

    if (hwnd_ && visible_) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
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
    
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        
    }
    
    
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
        {
            float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            input.on_mouse_scroll(delta);
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
