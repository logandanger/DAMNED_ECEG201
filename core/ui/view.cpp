#include "core/ui/view.hpp"

#include <cstdio>
#include <cstring>

#include "core/synth/presets.hpp"
#include "hal/hal.hpp"
#include "lvgl.h"

namespace danger {
namespace {

// Swiss palette: black on white, one signal red.
constexpr uint32_t kInk = 0x000000, kPaper = 0xFFFFFF, kGrey = 0x8C8C8C, kRule = 0xD0D0D0, kSignal = 0xE3000F;

constexpr int kTabH = 20, kScopeY = 20, kScopeH = 64;
constexpr int kRowsY = 85, kRowH = 22;
constexpr int kStatusY = kRowsY + kVisibleRows * kRowH + 1;
constexpr int kNameX = 8, kBarX = 112, kBarW = 112, kBarH = 6;
constexpr int kScopePoints = 160, kScopeSamples = 480, kScopeWindow = 1024, kScopeGain = 2;

constexpr int kDrawLines = 40;
alignas(4) uint8_t g_draw_buf[kScreenWidth * kDrawLines * 2];

struct RowView {
    lv_obj_t *box, *name, *value, *track, *fill;
    char name_text[24], value_text[24];
    int fill_w;
    bool selected, has_bar;
};

struct ViewState {
    lv_obj_t* tabs[kGroupCount];
    lv_obj_t* preset;
    lv_obj_t* scope;
    lv_point_precise_t points[kScopePoints];
    RowView rows[kVisibleRows];
    lv_obj_t *title, *status;
    char title_text[32], status_text[48], preset_text[24];
    int shown_group;
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

lv_obj_t* label(lv_obj_t* parent, int x, int y, int w, const lv_font_t* font, lv_text_align_t align) {
    lv_obj_t* o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_label_set_long_mode(o, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_align(o, align, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(kInk), 0);
    lv_label_set_text_static(o, "");
    return o;
}

void set_text(lv_obj_t* o, char* cache, size_t size, const char* text) {
    if (std::strcmp(cache, text) == 0) return;
    std::snprintf(cache, size, "%s", text);
    lv_label_set_text(o, cache);
}

void build_rows(lv_obj_t* screen) {
    for (int i = 0; i < kVisibleRows; ++i) {
        RowView& r = g.rows[i];
        r.box = box(screen, 0, kRowsY + i * kRowH, kScreenWidth, kRowH, kPaper);
        r.name = label(r.box, kNameX, 3, kBarX - kNameX - 4, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        r.track = box(r.box, kBarX, (kRowH - 1) / 2, kBarW, 1, kRule);
        r.fill = box(r.box, kBarX, (kRowH - kBarH) / 2, 0, kBarH, kInk);
        r.value = label(r.box, kBarX + kBarW + 4, 3, kScreenWidth - kBarX - kBarW - 4 - 8, &lv_font_montserrat_14,
                        LV_TEXT_ALIGN_RIGHT);
        r.fill_w = -1;
        r.has_bar = true;
    }
}

void set_selected(RowView& r, bool selected) {
    if (r.selected == selected) return;
    r.selected = selected;
    const lv_color_t fg = lv_color_hex(selected ? kPaper : kInk);
    lv_obj_set_style_bg_color(r.box, lv_color_hex(selected ? kInk : kPaper), 0);
    lv_obj_set_style_text_color(r.name, fg, 0);
    lv_obj_set_style_text_color(r.value, fg, 0);
    lv_obj_set_style_bg_color(r.fill, selected ? lv_color_hex(kSignal) : fg, 0);
    lv_obj_set_style_bg_color(r.track, lv_color_hex(selected ? kGrey : kRule), 0);
}

void set_bar(RowView& r, bool visible, int fill_w) {
    if (visible != r.has_bar) {
        r.has_bar = visible;
        if (visible) {
            lv_obj_remove_flag(r.track, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(r.fill, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(r.track, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r.fill, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (visible && fill_w != r.fill_w) {
        r.fill_w = fill_w;
        lv_obj_set_width(r.fill, fill_w);
    }
}

void update_row(RowView& r, const Menu& menu, const ParamStore& params, int index) {
    char value[24] = "";
    const char* name = "";
    bool bar = false;
    int fill = 0;
    if (index < menu.row_count()) {
        const Row row = menu.row(index);
        switch (row.kind) {
            case RowKind::Param: {
                const ParamDef& d = param_def(row.param);
                const float v = params.get(row.param);
                name = d.name;
                param_format(row.param, v, value, sizeof(value));
                bar = d.curve != Curve::Enum;
                fill = static_cast<int>(param_normalized(row.param, v) * kBarW + 0.5f);
                break;
            }
            case RowKind::Page:
                name = row.label;
                std::snprintf(value, sizeof(value), "%s",
                              row.target == PageId::Presets ? preset_name(menu.preset()) : ">");
                break;
            case RowKind::Preset:
                name = row.label;
                std::snprintf(value, sizeof(value), "%s", row.preset == menu.preset() ? "Loaded" : "");
                break;
        }
    }
    set_text(r.name, r.name_text, sizeof(r.name_text), name);
    set_text(r.value, r.value_text, sizeof(r.value_text), value);
    set_bar(r, bar, fill);
    set_selected(r, index == menu.cursor() && index < menu.row_count());
}

void update_tabs(int group) {
    if (group == g.shown_group) return;
    g.shown_group = group;
    for (int i = 0; i < kGroupCount; ++i) {
        const bool on = i == group;
        lv_obj_set_style_bg_color(g.tabs[i], lv_color_hex(on ? kInk : kPaper), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g.tabs[i], 0), lv_color_hex(on ? kPaper : kGrey), 0);
    }
}

void update_scope(const Engine& engine) {
    static int16_t samples[kScopeWindow];
    engine.read_scope(samples, kScopeWindow);

    // A rising zero crossing in the first half keeps periodic waves still.
    int start = 0;
    for (int i = 1; i < kScopeWindow - kScopeSamples; ++i) {
        if (samples[i - 1] < 0 && samples[i] >= 0) { start = i; break; }
    }
    constexpr int kMid = kScopeH / 2;
    constexpr int kStep = kScopeSamples / kScopePoints;
    bool changed = false;
    for (int i = 0; i < kScopePoints; ++i) {
        int y = kMid - (samples[start + i * kStep] * kScopeGain * (kMid - 2)) / 32768;
        y = y < 1 ? 1 : (y > kScopeH - 2 ? kScopeH - 2 : y);
        if (g.points[i].y != y) { g.points[i].y = y; changed = true; }
    }
    if (changed) lv_obj_invalidate(g.scope);
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

    constexpr int kTabW = 40;
    for (int i = 0; i < kGroupCount; ++i) {
        g.tabs[i] = box(screen, i * kTabW, 0, kTabW, kTabH - 1, kPaper);
        lv_obj_t* t = label(g.tabs[i], 0, 3, kTabW, &lv_font_montserrat_12, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text_static(t, group_short_name(i));
        lv_obj_set_style_text_color(t, lv_color_hex(kGrey), 0);
    }
    g.shown_group = -1;
    g.preset = label(screen, kGroupCount * kTabW, 3, kScreenWidth - kGroupCount * kTabW - 8, &lv_font_montserrat_12,
                     LV_TEXT_ALIGN_RIGHT);

    box(screen, 0, kScopeY - 1, kScreenWidth, 1, kInk);
    box(screen, 0, kScopeY + kScopeH / 2, kScreenWidth, 1, kRule);
    constexpr int kPointStep = kScreenWidth / kScopePoints;
    for (int i = 0; i < kScopePoints; ++i) {
        g.points[i].x = i * kPointStep;
        g.points[i].y = kScopeH / 2;
    }
    g.scope = lv_line_create(screen);
    lv_obj_set_pos(g.scope, 0, kScopeY);
    lv_obj_set_size(g.scope, kScreenWidth, kScopeH);
    lv_line_set_points_mutable(g.scope, g.points, kScopePoints);
    lv_obj_set_style_line_color(g.scope, lv_color_hex(kInk), 0);
    lv_obj_set_style_line_width(g.scope, 1, 0);
    box(screen, 0, kRowsY - 1, kScreenWidth, 1, kInk);

    build_rows(screen);

    box(screen, 0, kStatusY - 1, kScreenWidth, 1, kInk);
    g.title = label(screen, kNameX, kStatusY + 3, 130, &lv_font_montserrat_12, LV_TEXT_ALIGN_LEFT);
    g.status = label(screen, 130, kStatusY + 3, kScreenWidth - 130 - 8, &lv_font_montserrat_12, LV_TEXT_ALIGN_RIGHT);
}

void view_update(const Menu& menu, const ParamStore& params, const Engine& engine, float cpu_load) {
    update_tabs(menu.group());
    for (int i = 0; i < kVisibleRows; ++i) update_row(g.rows[i], menu, params, menu.top() + i);
    update_scope(engine);

    set_text(g.preset, g.preset_text, sizeof(g.preset_text), preset_name(menu.preset()));
    set_text(g.title, g.title_text, sizeof(g.title_text), menu.title());

    int voices = 0;
    for (uint8_t m = engine.voice_mask(); m; m = static_cast<uint8_t>(m & (m - 1))) ++voices;
    char status[48];
    std::snprintf(status, sizeof(status), "Oct %+d   Voices %d/%d   CPU %d %%", params.get_int(ParamId::GlbOctave),
                  voices, kVoices, static_cast<int>(cpu_load * 100.0f + 0.5f));
    set_text(g.status, g.status_text, sizeof(g.status_text), status);
}

}  // namespace danger
