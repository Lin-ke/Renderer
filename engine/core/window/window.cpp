#include "Window.h"
#include "engine/function/input/input.h"
#include <windowsx.h> // For GET_X_LPARAM, GET_Y_LPARAM

namespace Engine {

    Window::Window(int width, int height, const std::wstring& title) 
        : m_Width(width), m_Height(height), m_Title(title), m_hInstance(GetModuleHandle(nullptr)), m_ClassName(L"RendererWindowClass")
    {
        WNDCLASSEX wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = window_proc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = m_hInstance;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = m_ClassName.c_str();
        wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

        RegisterClassEx(&wc);

        m_hWnd = CreateWindowEx(
            0,
            m_ClassName.c_str(),
            m_Title.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            m_Width, m_Height,
            nullptr,
            nullptr,
            m_hInstance,
            this
        );

        if (m_hWnd) {
            ShowWindow(m_hWnd, SW_SHOW);
        }
    }

    Window::~Window() {
        if (m_hWnd) {
            DestroyWindow(m_hWnd);
        }
        UnregisterClass(m_ClassName.c_str(), m_hInstance);
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
        auto& input = Input::get_instance();

        switch (msg) {
            case WM_CLOSE:
                PostQuitMessage(0);
                return 0;
            
            case WM_KEYDOWN:
                input.on_key_down(static_cast<Key>(wParam));
                return 0;

            case WM_KEYUP:
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
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}
