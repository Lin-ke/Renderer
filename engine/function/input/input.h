#pragma once
#include <array>
#include "key_codes.h"

enum class InputState {
    None,
    Press,
    Repeat,
    Release
};

class Input {
public:
    static Input& get_instance() {
        static Input instance;
        return instance;
    }

    bool is_key_down(Key key) const;
    bool is_mouse_button_down(MouseButton button) const;
    bool is_key_pressed(Key key) const;
    bool is_mouse_button_pressed(MouseButton button) const;
    
    void get_mouse_position(int& x, int& y) const;
    void get_mouse_delta(float& dx, float& dy) const;
    void tick();

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

    std::array<InputState, 256> keys_ = { InputState::None };
    std::array<bool, 256> key_should_update_ = { false };
    
    std::array<InputState, 5> mouse_buttons_ = { InputState::None };
    std::array<bool, 5> mouse_button_should_update_ = { false };

    int mouse_x_ = 0;
    int mouse_y_ = 0;
    int last_mouse_x_ = 0;
    int last_mouse_y_ = 0;
    float mouse_delta_x_ = 0.0f;
    float mouse_delta_y_ = 0.0f;
};