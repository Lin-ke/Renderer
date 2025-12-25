#include "input.h"


bool Input::is_key_down(Key key) const {
    int key_code = static_cast<int>(key);
    if (key_code >= 0 && key_code < 256) {
        return keys_[key_code] == InputState::Press || keys_[key_code] == InputState::Repeat;
    }
    return false;
}

bool Input::is_mouse_button_down(MouseButton button) const {
    int button_code = static_cast<int>(button);
    if (button_code >= 0 && button_code < 5) {
        return mouse_buttons_[button_code] == InputState::Press || mouse_buttons_[button_code] == InputState::Repeat;
    }
    return false;
}

bool Input::is_key_pressed(Key key) const {
    int key_code = static_cast<int>(key);
    if (key_code >= 0 && key_code < 256) {
        return keys_[key_code] == InputState::Press;
    }
    return false;
}

bool Input::is_mouse_button_pressed(MouseButton button) const {
    int button_code = static_cast<int>(button);
    if (button_code >= 0 && button_code < 5) {
        return mouse_buttons_[button_code] == InputState::Press;
    }
    return false;
}

void Input::get_mouse_position(int& x, int& y) const {
    x = mouse_x_;
    y = mouse_y_;
}

void Input::get_mouse_delta(float& dx, float& dy) const {
    dx = mouse_delta_x_;
    dy = mouse_delta_y_;
}

void Input::tick() {
    // Calculate delta since last frame's position
    mouse_delta_x_ = static_cast<float>(mouse_x_ - last_mouse_x_);
    mouse_delta_y_ = static_cast<float>(mouse_y_ - last_mouse_y_);
    
    // Update last position for next frame
    last_mouse_x_ = mouse_x_;
    last_mouse_y_ = mouse_y_;

    // Handle key state transitions
    for (int i = 0; i < 256; ++i) {
        if (key_should_update_[i]) {
            keys_[i] = InputState::Repeat;
            key_should_update_[i] = false;
        }
        if (keys_[i] == InputState::Press) {
            key_should_update_[i] = true;
        }
    }

    for (int i = 0; i < 5; ++i) {
        if (mouse_button_should_update_[i]) {
            mouse_buttons_[i] = InputState::Repeat;
            mouse_button_should_update_[i] = false;
        }
        if (mouse_buttons_[i] == InputState::Press) {
            mouse_button_should_update_[i] = true;
        }
    }
}

void Input::on_key_down(Key key) {
    int key_code = static_cast<int>(key);
    if (key_code >= 0 && key_code < 256) {
        if (keys_[key_code] == InputState::None) {
            keys_[key_code] = InputState::Press;
        }
    }
}

void Input::on_key_up(Key key) {
    int key_code = static_cast<int>(key);
    if (key_code >= 0 && key_code < 256) {
        keys_[key_code] = InputState::None;
        key_should_update_[key_code] = false;
    }
}

void Input::on_mouse_move(int x, int y) {
    mouse_x_ = x;
    mouse_y_ = y;
}

void Input::on_mouse_button_down(MouseButton button) {
    int button_code = static_cast<int>(button);
    if (button_code >= 0 && button_code < 5) {
        if (mouse_buttons_[button_code] == InputState::None) {
            mouse_buttons_[button_code] = InputState::Press;
        }
    }
}

void Input::on_mouse_button_up(MouseButton button) {
    int button_code = static_cast<int>(button);
    if (button_code >= 0 && button_code < 5) {
        mouse_buttons_[button_code] = InputState::None;
        mouse_button_should_update_[button_code] = false;
    }
}
