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

        HWND get_hwnd() const { return hwnd_; }

    private:
        static LRESULT CALLBACK window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        HWND hwnd_;
        HINSTANCE hinstance_;
        std::wstring title_;
        std::wstring class_name_;
        int width_;
        int height_;
    };
}
