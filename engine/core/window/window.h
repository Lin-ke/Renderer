#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <string>

class Window {
public:
    Window(int width, int height, const std::wstring& title, bool visible = true);
    ~Window();

    // Returns false if the application should quit
    bool process_messages();

    HWND get_hwnd() const { return hwnd_; }
    bool is_visible() const { return visible_; }

private:
    static LRESULT CALLBACK window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_;
    HINSTANCE hinstance_;
    std::wstring title_;
    std::wstring class_name_;
    int width_;
    int height_;
    bool visible_;
};
