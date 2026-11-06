// On-screen replica of the box, laid out after docs/prototype1-sketch.jpg.
#pragma once
#include <SDL.h>

#include "sim/input.hpp"

namespace sim {

class Replica {
public:
    static constexpr int kWidth = 1200, kHeight = 900;  // logical size

    explicit Replica(Inputs* inputs) : inputs_(inputs) {}
    void handle(const SDL_Event& e);
    void draw(SDL_Renderer* r, SDL_Texture* screen, const uint8_t* leds);

private:
    void mouse_down(int x, int y, int button);
    void mouse_up();
    void mouse_move(int x, int y);
    void set_mouse_key(int key);

    Inputs* inputs_;
    int mouse_key_ = -1;
    bool mouse_button_held_ = false;
    hal::Control mouse_button_ = hal::Control::Up;
    int drag_enc_ = -1, drag_y_ = 0, drag_total_ = 0;
    bool dragged_ = false;
    bool latched_[hal::kControlCount] = {};
    int enc_angle_[2] = {};
    int mouse_x_ = 0, mouse_y_ = 0;
};

}  // namespace sim
