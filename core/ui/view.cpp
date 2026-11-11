#include "core/ui/view.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "core/config/beat_config.hpp"
#include "hal/hal.hpp"
#include "lvgl.h"

namespace danger {
namespace {

// Swiss palette: black on white, one signal red.
constexpr uint32_t kInk = 0x000000, kPaper = 0xFFFFFF, kGrey = 0x8C8C8C, kRule = 0xD0D0D0, kSignal = 0xE3000F;

constexpr int kMargin = 8;
constexpr int kContentW = kScreenWidth - 2 * kMargin;

// Grid, top to bottom.
constexpr int kHeadY = 0, kHeadH = 48;
constexpr int kLoopY = kHeadY + kHeadH + 1, kLoopBarsY = kLoopY + 18, kLoopBarsH = 16;
constexpr int kLayersY = kLoopBarsY + kLoopBarsH + 6, kLayerRowY = kLayersY + 18, kLayerRowH = 15;
constexpr int kStatusY = kLayerRowY + kLayerSlots * kLayerRowH + 2;

constexpr int kBpmX = 168, kRevX = 244;
constexpr int kLayerStateX = 120, kLayerBarX = 222, kLayerBarW = kScreenWidth - kMargin - kLayerBarX;
constexpr int kHoldBarX = 232, kHoldBarW = kScreenWidth - kMargin - kHoldBarX;

constexpr int kDrawLines = 40;
alignas(4) uint8_t g_draw_buf[kScreenWidth * kDrawLines * 2];

struct Text {
    lv_obj_t* obj;
    char cache[48];
    uint32_t color;
};

struct Bar {
    lv_obj_t *track, *fill;
    int w, full;
    uint32_t color;
};

struct LayerRow {
    Text index, name, state;
    lv_obj_t* mark;
    Bar bar;
    bool mark_on, visible;
};

struct ViewState {
    Text inst_cap, inst, bpm, rev, loop_cap, bar_text, layers_cap, cpu, seed, hold;
    lv_obj_t* playhead;
    int playhead_x;
    lv_obj_t* bar_box[cfg::kLoopBars];
    int shown_bar;
    LayerRow rows[kLayerSlots];
    Bar seed_bar, hold_bar;
    lv_obj_t* hold_tick;
} g;

void flush_cb(lv_display_t* disp, const lv_area_t* a, uint8_t* px) {
    hal::display_flush(a->x1, a->y1, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1, reinterpret_cast<uint16_t*>(px));
    lv_display_flush_ready(disp);
}

uint32_t tick_cb() { return hal::millis(); }

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

Text label(lv_obj_t* parent, int x, int y, int w, const lv_font_t* font, lv_text_align_t align, uint32_t color) {
    lv_obj_t* o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_label_set_long_mode(o, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_align(o, align, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_label_set_text_static(o, "");
    Text t{o, "", color};
    return t;
}

void set_text(Text& t, const char* text) {
    if (std::strcmp(t.cache, text) == 0) return;
    std::snprintf(t.cache, sizeof(t.cache), "%s", text);
    lv_label_set_text(t.obj, t.cache);
}

void set_color(Text& t, uint32_t color) {
    if (t.color == color) return;
    t.color = color;
    lv_obj_set_style_text_color(t.obj, lv_color_hex(color), 0);
}

Bar bar(lv_obj_t* parent, int x, int y, int w) {
    Bar b;
    b.track = box(parent, x, y + 2, w, 1, kRule);
    b.fill = box(parent, x, y, 0, 5, kInk);
    b.w = 0;
    b.full = w;
    b.color = kInk;
    return b;
}

void set_bar(Bar& b, float v, uint32_t color) {
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    const int w = static_cast<int>(v * static_cast<float>(b.full) + 0.5f);
    if (w != b.w) {
        b.w = w;
        lv_obj_set_width(b.fill, w);
    }
    if (color != b.color) {
        b.color = color;
        lv_obj_set_style_bg_color(b.fill, lv_color_hex(color), 0);
    }
}

void show(lv_obj_t* o, bool on) {
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

const char* inst_name(int i) { return i >= 0 && i < kInstrumentCount ? kInstrumentSamples[i].name : ""; }

// ---- Sections ----------------------------------------------------------------------------------

void update_head(const ViewModel& m) {
    const bool browsing = m.browse != m.s.instrument;
    set_text(g.inst_cap, browsing ? "HOLD A TO SELECT" : "INSTRUMENT");
    set_color(g.inst_cap, browsing ? kSignal : kGrey);
    set_text(g.inst, inst_name(m.browse));
    set_color(g.inst, browsing ? kSignal : kInk);
    char t[16];
    std::snprintf(t, sizeof(t), "%d", static_cast<int>(m.s.bpm + 0.5f));
    set_text(g.bpm, t);
    std::snprintf(t, sizeof(t), "%d%%", static_cast<int>(m.s.reverb * 100.0f + 0.5f));
    set_text(g.rev, t);
}

void update_loop(const ViewModel& m) {
    const float pos = m.s.loop_pos;
    const int x = kMargin + static_cast<int>(pos / cfg::kLoopSteps * static_cast<float>(kContentW));
    if (x != g.playhead_x) {
        g.playhead_x = x;
        lv_obj_set_x(g.playhead, x > kMargin + kContentW - 2 ? kMargin + kContentW - 2 : x);
    }
    const int bar = static_cast<int>(pos) / cfg::kStepsPerBar;
    if (bar != g.shown_bar) {
        g.shown_bar = bar;
        for (int i = 0; i < cfg::kLoopBars; ++i)
            lv_obj_set_style_border_color(g.bar_box[i], lv_color_hex(i == bar ? kInk : kRule), 0);
    }
    char t[24];
    const int beat = static_cast<int>(pos) % cfg::kStepsPerBar / cfg::kStepsPerBeat;
    std::snprintf(t, sizeof(t), "BAR %d.%d / %d", bar + 1, beat + 1, cfg::kLoopBars);
    set_text(g.bar_text, t);
}

void update_layers(const ViewModel& m) {
    int active = 0;
    for (int i = 0; i < m.s.layer_count; ++i) active += m.s.layers[m.s.order[i]].state == LayerState::Active;
    char t[32];
    std::snprintf(t, sizeof(t), "LAYERS %d / %d", active, cfg::kMaxLayers);
    set_text(g.layers_cap, t);
    std::snprintf(t, sizeof(t), "CPU %d%%", static_cast<int>(m.cpu * 100.0f + 0.5f));
    set_text(g.cpu, t);

    for (int r = 0; r < kLayerSlots; ++r) {
        LayerRow& row = g.rows[r];
        const bool visible = r < m.s.layer_count;
        if (visible != row.visible) {
            row.visible = visible;
            for (lv_obj_t* o : {row.index.obj, row.name.obj, row.state.obj, row.bar.track, row.bar.fill}) show(o, visible);
        }
        if (!visible) {
            if (row.mark_on) { row.mark_on = false; show(row.mark, false); }
            continue;
        }
        const LayerView& l = m.s.layers[m.s.order[r]];
        std::snprintf(t, sizeof(t), "%d", r + 1);
        set_text(row.index, t);
        set_text(row.name, inst_name(l.instrument));
        const bool out = l.state == LayerState::FadingOut;
        if (out) std::snprintf(t, sizeof(t), "FADE OUT %d%%", static_cast<int>(l.gain * 100.0f + 0.5f));
        else if (l.fade_in < 1.0f) std::snprintf(t, sizeof(t), "FADE IN %d%%", static_cast<int>(l.fade_in * 100.0f));
        else std::snprintf(t, sizeof(t), "PLAYING  %d", l.notes);
        set_text(row.state, t);
        const uint32_t ink = out ? kGrey : kInk;
        set_color(row.name, ink);
        set_color(row.state, ink);
        set_bar(row.bar, out ? l.gain : l.fade_in, out ? kGrey : kInk);
        if (l.current != row.mark_on) {
            row.mark_on = l.current;
            show(row.mark, l.current);
        }
    }
}

void update_status(const ViewModel& m) {
    char t[48];
    const EngineStatus& s = m.s;
    if (s.seed_notes && s.keys_down) {
        std::snprintf(t, sizeof(t), "SEED  %d NOTE%s", s.seed_notes, s.seed_notes == 1 ? "" : "S");
        set_bar(g.seed_bar, 1, kInk);
    } else if (s.seed_notes) {
        const float left = s.commit_in < 0 ? 0 : s.commit_in;
        std::snprintf(t, sizeof(t), "SEED  %d NOTE%s  COMMIT %.1f s", s.seed_notes, s.seed_notes == 1 ? "" : "S",
                      static_cast<double>(left));
        set_bar(g.seed_bar, left / cfg::kSeedIdleCommit, kSignal);
    } else {
        bool has_layer = false;
        for (const LayerView& l : s.layers) has_layer = has_layer || (l.current && l.instrument == s.instrument &&
                                                                      l.state == LayerState::Active);
        std::snprintf(t, sizeof(t), "%s", has_layer ? "PLAY TO ADD TO LAYER" : "PLAY KEYS TO SEED");
        set_bar(g.seed_bar, 0, kInk);
    }
    set_text(g.seed, t);

    switch (m.mode) {
        case ButtonLogic::Mode::AOnly:
            std::snprintf(t, sizeof(t), "HOLD A: SELECT %s", inst_name(m.browse));
            set_text(g.hold, t);
            set_bar(g.hold_bar, m.hold, kInk);
            show(g.hold_tick, false);
            break;
        case ButtonLogic::Mode::Chord: {
            const float layer_at = cfg::kClearLayerHold / cfg::kClearAllHold;
            bool target = false;  // a layer is marked; otherwise A+B drops the pending seed
            for (const LayerView& l : s.layers) target = target || (l.current && l.state == LayerState::Active);
            const char* release = target || !s.seed_notes ? "RELEASE: CLEAR LAYER" : "RELEASE: CLEAR SEED";
            set_text(g.hold, m.hold < layer_at ? "A+B: HOLD TO CLEAR" : (m.hold < 1 ? release : "CLEAR ALL"));
            set_bar(g.hold_bar, m.hold, m.hold < layer_at ? kInk : kSignal);
            show(g.hold_tick, true);
            break;
        }
        default:
            set_text(g.hold, "A/B INSTRUMENT  A+B CLEAR");
            set_bar(g.hold_bar, 0, kInk);
            show(g.hold_tick, false);
            break;
    }
}

}  // namespace

void view_init() {
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t* disp = lv_display_create(kScreenWidth, kScreenHeight);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, g_draw_buf, nullptr, sizeof(g_draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kPaper), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    const lv_font_t* small = &lv_font_montserrat_12;
    const lv_font_t* body = &lv_font_montserrat_14;
    const lv_font_t* big = &lv_font_montserrat_24;

    // Head: instrument | BPM | reverb.
    g.inst_cap = label(screen, kMargin, 5, kBpmX - kMargin - 8, small, LV_TEXT_ALIGN_LEFT, kGrey);
    g.inst = label(screen, kMargin, 19, kBpmX - kMargin - 8, big, LV_TEXT_ALIGN_LEFT, kInk);
    Text cap = label(screen, kBpmX, 5, kRevX - kBpmX - 8, small, LV_TEXT_ALIGN_LEFT, kGrey);
    lv_label_set_text_static(cap.obj, "BPM");
    g.bpm = label(screen, kBpmX, 19, kRevX - kBpmX - 8, big, LV_TEXT_ALIGN_LEFT, kInk);
    cap = label(screen, kRevX, 5, kScreenWidth - kMargin - kRevX, small, LV_TEXT_ALIGN_LEFT, kGrey);
    lv_label_set_text_static(cap.obj, "REVERB");
    g.rev = label(screen, kRevX, 19, kScreenWidth - kMargin - kRevX, big, LV_TEXT_ALIGN_LEFT, kInk);
    box(screen, kBpmX - 8, 6, 1, kHeadH - 12, kRule);
    box(screen, kRevX - 8, 6, 1, kHeadH - 12, kRule);
    box(screen, 0, kHeadH, kScreenWidth, 1, kInk);

    // Loop: four bars, beat ticks, red playhead.
    g.loop_cap = label(screen, kMargin, kLoopY + 2, 100, small, LV_TEXT_ALIGN_LEFT, kGrey);
    lv_label_set_text_static(g.loop_cap.obj, "LOOP");
    g.bar_text = label(screen, 160, kLoopY + 2, kScreenWidth - kMargin - 160, small, LV_TEXT_ALIGN_RIGHT, kInk);
    constexpr int kBarW = kContentW / cfg::kLoopBars;
    for (int b = 0; b < cfg::kLoopBars; ++b) {
        lv_obj_t* o = box(screen, kMargin + b * kBarW, kLoopBarsY, kBarW - 2, kLoopBarsH, kPaper);
        lv_obj_set_style_border_width(o, 1, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(kRule), 0);
        g.bar_box[b] = o;
        for (int beat = 1; beat < cfg::kBeatsPerBar; ++beat)
            box(screen, kMargin + b * kBarW + beat * kBarW / cfg::kBeatsPerBar, kLoopBarsY + kLoopBarsH - 5, 1, 4, kGrey);
    }
    g.shown_bar = -1;
    g.playhead = box(screen, kMargin, kLoopBarsY - 2, 2, kLoopBarsH + 4, kSignal);
    g.playhead_x = -1;
    box(screen, 0, kLayersY - 3, kScreenWidth, 1, kInk);

    // Layers, oldest first.
    g.layers_cap = label(screen, kMargin, kLayersY, 200, small, LV_TEXT_ALIGN_LEFT, kGrey);
    g.cpu = label(screen, 220, kLayersY, kScreenWidth - kMargin - 220, small, LV_TEXT_ALIGN_RIGHT, kGrey);
    for (int r = 0; r < kLayerSlots; ++r) {
        LayerRow& row = g.rows[r];
        const int y = kLayerRowY + r * kLayerRowH;
        row.index = label(screen, kMargin, y, 12, small, LV_TEXT_ALIGN_LEFT, kGrey);
        row.mark = box(screen, 22, y + 4, 6, 6, kSignal);
        row.name = label(screen, 34, y - 1, kLayerStateX - 38, body, LV_TEXT_ALIGN_LEFT, kInk);
        row.state = label(screen, kLayerStateX, y, kLayerBarX - kLayerStateX - 6, small, LV_TEXT_ALIGN_LEFT, kInk);
        row.bar = bar(screen, kLayerBarX, y + 4, kLayerBarW);
        show(row.mark, false);
        row.visible = true;
        row.mark_on = false;
    }
    box(screen, 0, kStatusY - 2, kScreenWidth, 1, kInk);

    // Status: seed state with countdown, A/B hold progress with the clear-layer mark.
    g.seed = label(screen, kMargin, kStatusY + 2, kHoldBarX - kMargin - 6, small, LV_TEXT_ALIGN_LEFT, kInk);
    g.seed_bar = bar(screen, kHoldBarX, kStatusY + 6, kHoldBarW);
    g.hold = label(screen, kMargin, kStatusY + 20, kHoldBarX - kMargin - 6, small, LV_TEXT_ALIGN_LEFT, kInk);
    g.hold_bar = bar(screen, kHoldBarX, kStatusY + 24, kHoldBarW);
    g.hold_tick = box(screen, kHoldBarX + static_cast<int>(kHoldBarW * cfg::kClearLayerHold / cfg::kClearAllHold),
                      kStatusY + 20, 1, 13, kInk);
    show(g.hold_tick, false);
}

void view_update(const ViewModel& m) {
    update_head(m);
    update_loop(m);
    update_layers(m);
    update_status(m);
}

}  // namespace danger
