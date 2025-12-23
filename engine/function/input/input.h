#pragma once
#include <array>
#include "key_codes.h"

namespace Engine {
    class Input {
    public:
        static Input& get_instance() {
            static Input instance;
            return instance;
        }

        bool is_key_down(Key key) const;
        bool is_mouse_button_down(MouseButton button) const;
        void get_mouse_position(int& x, int& y) const;

        // Internal update methods called by Window
        void on_key_down(Key key);
        void on_key_up(Key key);
        void on_mouse_move(int x, int y);
        void on_mouse_button_down(MouseButton button);
        void on_mouse_button_up(MouseButton button);

    private:
        Input() = default;
        ~Input() = default;
        Input(const Input&) = delete;
        Input& operator=(const Input&) = delete;

        std::array<bool, 256> m_keys = { false };
        std::array<bool, 5> m_mouse_buttons = { false };
        int m_mouse_x = 0;
        int m_mouse_y = 0;
    };
}