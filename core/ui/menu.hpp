// Menu state machine. No LVGL here, so it runs in the unit tests.
#pragma once
#include "core/synth/params.hpp"
#include "core/ui/actions.hpp"

namespace danger {

enum class Group : uint8_t { Oscillator, Filter, Envelope, Effects, Global, Count };
constexpr int kGroupCount = static_cast<int>(Group::Count);
const char* group_short_name(int group);

enum class PageId : uint8_t {
    Oscillator, Filter, Envelope, Effects, Global,  // top level, same order as Group
    Drive, Modulation, Delay, Reverb, Presets
};

enum class RowKind : uint8_t { Param, Page, Preset };

struct Row {
    RowKind kind;
    ParamId param;      // Param
    PageId target;      // Page
    const char* label;  // Page and Preset
    int preset;         // Preset
};

constexpr int kVisibleRows = 6;

class Menu {
public:
    explicit Menu(ParamStore* params) : params_(params) {}

    void handle(const MappedAction& a);
    void load_preset(int index);

    int group() const { return static_cast<int>(group_); }
    PageId page() const { return page_; }
    bool in_subpage() const { return static_cast<int>(page_) >= kGroupCount; }
    const char* title() const;
    int row_count() const;
    Row row(int index) const;
    int cursor() const { return cursor_; }
    int top() const { return top_; }
    int preset() const { return preset_; }

private:
    void set_cursor(int cursor);
    void enter(PageId page);
    void leave();
    void switch_group(int delta);
    void adjust(int detents, bool coarse);
    void select();
    void reset_value();

    ParamStore* params_;
    Group group_ = Group::Oscillator;
    PageId page_ = PageId::Oscillator;
    int cursor_ = 0, top_ = 0;
    int parent_cursor_ = 0, parent_top_ = 0;
    int preset_ = 0;
};

}  // namespace danger
