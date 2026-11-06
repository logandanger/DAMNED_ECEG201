// Merges mouse, keyboard and latches: a control is held while any source holds it.
#pragma once
#include <SDL.h>

#include <vector>

#include "core/config/control_map.hpp"
#include "hal/sim/hal_sim.hpp"

namespace sim {

class Inputs {
public:
    void press(hal::Control c) {
        if (held_[int(c)]++ == 0) {
            hal::sim::emit({c, hal::EventType::Press, 0, hal::is_key(c) ? hal::kFixedKeyVelocity : uint8_t(0)});
        }
    }
    void release(hal::Control c) {
        if (held_[int(c)] > 0 && --held_[int(c)] == 0) hal::sim::emit({c, hal::EventType::Release, 0, 0});
    }
    void turn(hal::Control c, int detents) { hal::sim::emit({c, hal::EventType::Turn, int16_t(detents), 0}); }
    bool held(hal::Control c) const { return held_[int(c)] > 0; }

private:
    int held_[hal::kControlCount] = {};
};

// Computer keyboard, driven by kPcKeys in the mapping file.
class Keyboard {
public:
    explicit Keyboard(Inputs* inputs) : inputs_(inputs) {
        for (const danger::PcKey& k : danger::kPcKeys) {
            const SDL_Scancode s = SDL_GetScancodeFromName(k.key);
            if (s != SDL_SCANCODE_UNKNOWN) keys_.push_back({s, k.control, k.turn, false});
        }
    }

    void handle(const SDL_Event& e) {
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            release_all();  // the key-up goes to another window, which would leave a stuck note
            return;
        }
        if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP) return;
        for (Key& k : keys_) {
            if (k.scancode != e.key.keysym.scancode) continue;
            if (k.turn != 0) {
                if (e.type == SDL_KEYDOWN) inputs_->turn(k.control, k.turn);  // repeats spin the encoder
            } else if (e.type == SDL_KEYDOWN && !k.down) {
                k.down = true;
                inputs_->press(k.control);
            } else if (e.type == SDL_KEYUP && k.down) {
                k.down = false;
                inputs_->release(k.control);
            }
        }
    }

private:
    struct Key { SDL_Scancode scancode; hal::Control control; int8_t turn; bool down; };

    void release_all() {
        for (Key& k : keys_) {
            if (k.down) { k.down = false; inputs_->release(k.control); }
        }
    }

    Inputs* inputs_;
    std::vector<Key> keys_;
};

}  // namespace sim
