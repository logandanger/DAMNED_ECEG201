#include "sim/replica.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sim {
namespace {

using hal::Control;

struct Color { Uint8 r, g, b; };
constexpr Color kCase = {240, 239, 234}, kInk = {0, 0, 0}, kPaper = {255, 255, 255}, kGrey = {140, 140, 140},
                kSignal = {227, 0, 15}, kPanel = {28, 28, 28};

void ink(SDL_Renderer* r, Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255); }
void fill(SDL_Renderer* r, const SDL_Rect& q, Color c) { ink(r, c); SDL_RenderFillRect(r, &q); }
void frame(SDL_Renderer* r, const SDL_Rect& q, Color c) {
    ink(r, c);
    for (int i = 0; i < 2; ++i) {
        const SDL_Rect o = {q.x + i, q.y + i, q.w - 2 * i, q.h - 2 * i};
        SDL_RenderDrawRect(r, &o);
    }
}

// Columns of a 5x7 font, bit 0 on top. Only the characters the replica prints.
const Uint8* glyph(char c) {
    static const Uint8 digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E}};
    static const Uint8 letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}};
    static const Uint8 space[5] = {0, 0, 0, 0, 0}, semi[5] = {0x00,0x56,0x36,0x00,0x00},
                       comma[5] = {0x00,0x50,0x30,0x00,0x00}, dot[5] = {0x00,0x60,0x60,0x00,0x00},
                       slash[5] = {0x20,0x10,0x08,0x04,0x02}, dash[5] = {0x08,0x08,0x08,0x08,0x08};
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    switch (c) {
        case ';': return semi;
        case ',': return comma;
        case '.': return dot;
        case '/': return slash;
        case '-': return dash;
        default: return space;
    }
}

int text_width(const char* s, int scale) { return int(std::strlen(s)) * 6 * scale - scale; }

void text(SDL_Renderer* r, int x, int y, const char* s, int scale, Color c) {
    ink(r, c);
    for (; *s; ++s, x += 6 * scale) {
        const Uint8* g = glyph(*s);
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if (g[col] & (1 << row)) {
                    const SDL_Rect p = {x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(r, &p);
                }
            }
        }
    }
}

void text_centered(SDL_Renderer* r, const SDL_Rect& q, const char* s, int scale, Color c) {
    text(r, q.x + (q.w - text_width(s, scale)) / 2, q.y + (q.h - 7 * scale) / 2, s, scale, c);
}

void disc(SDL_Renderer* r, int cx, int cy, int radius, Color c) {
    ink(r, c);
    for (int dy = -radius; dy <= radius; ++dy) {
        const int dx = int(std::sqrt(double(radius * radius - dy * dy)));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

// ---- Geometry, in logical pixels ---------------------------------------------------------------
constexpr SDL_Rect kScreen = {260, 40, 640, 480};
constexpr SDL_Rect kStrip = {260, 536, 640, 60};
constexpr SDL_Rect kSpeaker = {1060, 40, 110, 230};
constexpr int kKeysY = 626, kKeysH = 250, kKeysX = 24, kKeysW = 1152;
constexpr int kBlackH = 130, kBlackW = 100;

struct Button { Control control; SDL_Rect rect; const char* label; };
const Button kButtons[] = {
    {Control::Up,    {98, 196, 76, 60},  "UP"},
    {Control::Down,  {98, 324, 76, 60},  "DOWN"},
    {Control::Left,  {24, 260, 76, 60},  "LEFT"},
    {Control::Right, {172, 260, 76, 60}, "RIGHT"},
    {Control::A,     {980, 214, 76, 56}, "A"},
    {Control::B,     {916, 300, 76, 56}, "B"},
};

struct Encoder { Control control; int x, y; const char* label; };
constexpr int kEncRadius = 44;
const Encoder kEncoders[] = {{Control::Enc1, 1115, 346, "ENC 1"}, {Control::Enc2, 1115, 490, "ENC 2"}};

struct KeyShape { SDL_Rect rect; bool black; const char* hint; };
KeyShape key_shape(int key) {
    static const int white_of[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    static const int black_after[12] = {-1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1};
    static const char* const hints[12] = {"A", "W", "S", "E", "D", "F", "T", "G", "Y", "H", "U", "J"};
    const float w = float(kKeysW) / 7.0f;
    if (white_of[key] >= 0) {
        const int x0 = kKeysX + int(w * float(white_of[key]) + 0.5f);
        const int x1 = kKeysX + int(w * float(white_of[key] + 1) + 0.5f);
        return {{x0, kKeysY, x1 - x0 + 2, kKeysH}, false, hints[key]};
    }
    const int cx = kKeysX + int(w * float(black_after[key] + 1) + 0.5f);
    return {{cx - kBlackW / 2, kKeysY, kBlackW, kBlackH}, true, hints[key]};
}

bool inside(const SDL_Rect& q, int x, int y) { return x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h; }

int key_at(int x, int y) {
    for (int k = 0; k < 12; ++k) {
        const KeyShape s = key_shape(k);
        if (s.black && inside(s.rect, x, y)) return k;
    }
    for (int k = 0; k < 12; ++k) {
        const KeyShape s = key_shape(k);
        if (!s.black && inside(s.rect, x, y)) return k;
    }
    return -1;
}

int encoder_at(int x, int y) {
    for (int i = 0; i < 2; ++i) {
        const int dx = x - kEncoders[i].x, dy = y - kEncoders[i].y;
        if (dx * dx + dy * dy <= kEncRadius * kEncRadius) return i;
    }
    return -1;
}

const Button* button_at(int x, int y) {
    for (const Button& b : kButtons) {
        if (inside(b.rect, x, y)) return &b;
    }
    return nullptr;
}

constexpr int kDragPixelsPerDetent = 8;
constexpr int kDegreesPerDetent = 18;  // 20 detents per turn

}  // namespace

void Replica::set_mouse_key(int key) {
    if (key == mouse_key_) return;
    if (mouse_key_ >= 0) inputs_->release(hal::key_control(mouse_key_));
    mouse_key_ = key;
    if (key >= 0) inputs_->press(hal::key_control(key));
}

void Replica::mouse_down(int x, int y, int button) {
    if (const Button* b = button_at(x, y)) {
        if (button == SDL_BUTTON_RIGHT) {  // latch, so chords work with one mouse pointer
            bool& l = latched_[int(b->control)];
            l = !l;
            if (l) inputs_->press(b->control);
            else inputs_->release(b->control);
        } else if (button == SDL_BUTTON_LEFT) {
            inputs_->press(b->control);
            mouse_button_ = b->control;
            mouse_button_held_ = true;
        }
        return;
    }
    if (button != SDL_BUTTON_LEFT) return;
    const int enc = encoder_at(x, y);
    if (enc >= 0) {
        drag_enc_ = enc;
        drag_y_ = y;
        drag_total_ = 0;
        dragged_ = false;
        return;
    }
    set_mouse_key(key_at(x, y));
}

void Replica::mouse_up() {
    set_mouse_key(-1);
    if (mouse_button_held_) {
        inputs_->release(mouse_button_);
        mouse_button_held_ = false;
    }
    if (drag_enc_ >= 0) {
        if (!dragged_) {  // a click without a drag is a push
            inputs_->press(kEncoders[drag_enc_].control);
            inputs_->release(kEncoders[drag_enc_].control);
        }
        drag_enc_ = -1;
    }
}

void Replica::mouse_move(int x, int y) {
    if (mouse_key_ >= 0) {
        const int k = key_at(x, y);
        if (k >= 0) set_mouse_key(k);  // glide along the keyboard
    }
    if (drag_enc_ >= 0) {
        const int detents = (drag_y_ - y) / kDragPixelsPerDetent;
        if (detents != 0) {
            drag_y_ -= detents * kDragPixelsPerDetent;
            dragged_ = true;
            enc_angle_[drag_enc_] += detents * kDegreesPerDetent;
            inputs_->turn(kEncoders[drag_enc_].control, detents);
        }
    }
}

void Replica::handle(const SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEBUTTONDOWN: mouse_down(e.button.x, e.button.y, e.button.button); break;
        case SDL_MOUSEBUTTONUP: if (e.button.button == SDL_BUTTON_LEFT) mouse_up(); break;
        case SDL_MOUSEMOTION:
            mouse_x_ = e.motion.x;
            mouse_y_ = e.motion.y;
            mouse_move(e.motion.x, e.motion.y);
            break;
        case SDL_MOUSEWHEEL: {
            const int enc = encoder_at(mouse_x_, mouse_y_);
            if (enc >= 0 && e.wheel.y != 0) {
                enc_angle_[enc] += e.wheel.y * kDegreesPerDetent;
                inputs_->turn(kEncoders[enc].control, e.wheel.y);
            }
            break;
        }
        default: break;
    }
}

void Replica::draw(SDL_Renderer* r, SDL_Texture* screen, const uint8_t* leds) {
    ink(r, kCase);
    SDL_RenderClear(r);

    frame(r, {kScreen.x - 2, kScreen.y - 2, kScreen.w + 4, kScreen.h + 4}, kInk);
    SDL_RenderCopy(r, screen, nullptr, &kScreen);

    fill(r, kStrip, kPanel);
    constexpr int kCols = 16, kCellW = 36, kCellH = 22, kGapX = 4, kGapY = 6;
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const int i = row * kCols + col;
            const SDL_Rect c = {kStrip.x + 2 + col * (kCellW + kGapX), kStrip.y + 5 + row * (kCellH + kGapY), kCellW, kCellH};
            // Square-root display curve: the default LED level is low and would be unreadable linearly.
            const auto lift = [](uint8_t v) { return Uint8(std::sqrt(double(v) / 255.0) * 255.0); };
            const Color on = {lift(leds[i * 3]), lift(leds[i * 3 + 1]), lift(leds[i * 3 + 2])};
            fill(r, c, (on.r | on.g | on.b) ? on : Color{52, 52, 52});
            if (i >= 20) frame(r, c, kGrey);  // not on the board: see README risk R4
        }
    }
    text(r, kStrip.x, kStrip.y + kStrip.h + 6, "OUTLINED CELLS ARE NOT ON THE BOARD", 2, kGrey);

    frame(r, kSpeaker, kInk);
    for (int x = kSpeaker.x + 12; x < kSpeaker.x + kSpeaker.w - 8; x += 10) {
        const SDL_Rect slot = {x, kSpeaker.y + 12, 2, kSpeaker.h - 24};
        fill(r, slot, kInk);
    }

    for (const Button& b : kButtons) {
        const bool held = inputs_->held(b.control);
        fill(r, b.rect, held ? kInk : kPaper);
        frame(r, b.rect, latched_[int(b.control)] ? kSignal : kInk);
        text_centered(r, b.rect, b.label, 2, held ? kPaper : kInk);
    }

    for (int i = 0; i < 2; ++i) {
        const Encoder& e = kEncoders[i];
        const bool held = inputs_->held(e.control);
        disc(r, e.x, e.y, kEncRadius, kInk);
        disc(r, e.x, e.y, kEncRadius - 3, held ? kInk : kPaper);
        const double a = double(enc_angle_[i]) * 3.14159265 / 180.0;
        const int tx = e.x + int(std::sin(a) * (kEncRadius - 8)), ty = e.y - int(std::cos(a) * (kEncRadius - 8));
        disc(r, tx, ty, 4, held ? kPaper : kSignal);
        const SDL_Rect cap = {e.x - 60, e.y + kEncRadius + 6, 120, 14};
        text_centered(r, cap, e.label, 2, kInk);
    }

    for (int pass = 0; pass < 2; ++pass) {  // white keys first, black on top
        for (int k = 0; k < 12; ++k) {
            const KeyShape s = key_shape(k);
            if (s.black != (pass == 1)) continue;
            const bool held = inputs_->held(hal::key_control(k));
            fill(r, s.rect, held ? kSignal : (s.black ? kInk : kPaper));
            frame(r, s.rect, kInk);
            const SDL_Rect hint = {s.rect.x, s.rect.y + s.rect.h - 30, s.rect.w, 20};
            text_centered(r, hint, s.hint, 2, (s.black || held) ? kPaper : kGrey);
        }
    }

    text(r, 24, 40, "DANGER", 3, kInk);
    text(r, 24, 70, "PROTOTYPE 1", 2, kGrey);
}

}  // namespace sim
