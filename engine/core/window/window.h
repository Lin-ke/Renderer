#pragma once
#include <Windows.h>
#include <string>

namespace Engine {
    class Window {
    public:
        Window(int width, int height, const std::wstring& title);
        ~Window();

        // Returns false if the application should quit
        bool process_messages();

        HWND get_hwnd() const { return m_hWnd; }

    private:
        static LRESULT CALLBACK window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        HWND m_hWnd;
        HINSTANCE m_hInstance;
        std::wstring m_Title;
        std::wstring m_ClassName;
        int m_Width;
        int m_Height;
    };
}
