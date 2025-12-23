#include "input.h"

namespace Engine {

    bool Input::is_key_down(Key key) const {
        int key_code = static_cast<int>(key);
        if (key_code >= 0 && key_code < 256) {
            return m_keys[key_code];
        }
        return false;
    }

    bool Input::is_mouse_button_down(MouseButton button) const {
        int button_code = static_cast<int>(button);
        if (button_code >= 0 && button_code < 5) {
            return m_mouse_buttons[button_code];
        }
        return false;
    }

    void Input::get_mouse_position(int& x, int& y) const {
        x = m_mouse_x;
        y = m_mouse_y;
    }

    void Input::on_key_down(Key key) {
        int key_code = static_cast<int>(key);
        if (key_code >= 0 && key_code < 256) {
            m_keys[key_code] = true;
        }
    }

    void Input::on_key_up(Key key) {
        int key_code = static_cast<int>(key);
        if (key_code >= 0 && key_code < 256) {
            m_keys[key_code] = false;
        }
    }

    void Input::on_mouse_move(int x, int y) {
        m_mouse_x = x;
        m_mouse_y = y;
    }

    void Input::on_mouse_button_down(MouseButton button) {
        int button_code = static_cast<int>(button);
        if (button_code >= 0 && button_code < 5) {
            m_mouse_buttons[button_code] = true;
        }
    }

    void Input::on_mouse_button_up(MouseButton button) {
        int button_code = static_cast<int>(button);
        if (button_code >= 0 && button_code < 5) {
            m_mouse_buttons[button_code] = false;
        }
    }
}