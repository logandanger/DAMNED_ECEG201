#include "core/ui/menu.hpp"

#include "core/synth/presets.hpp"

namespace danger {
namespace {

using P = ParamId;

constexpr Row param(ParamId id) { return {RowKind::Param, id, PageId::Oscillator, nullptr, 0}; }
constexpr Row link(PageId target, const char* label) { return {RowKind::Page, P::OscWave, target, label, 0}; }

const Row kOscRows[] = {param(P::OscWave), param(P::OscSemi), param(P::OscFine), param(P::OscPw), param(P::OscLevel)};
const Row kFilterRows[] = {param(P::FltType), param(P::FltCutoff), param(P::FltReso), param(P::FltEnvAmt),
                           param(P::FltKeyTrack)};
const Row kEnvRows[] = {param(P::AmpA), param(P::AmpD), param(P::AmpS), param(P::AmpR),
                        param(P::FenvA), param(P::FenvD), param(P::FenvS), param(P::FenvR)};
const Row kFxRows[] = {link(PageId::Drive, "Drive / crush"), link(PageId::Modulation, "Chorus / phaser"),
                       link(PageId::Delay, "Delay"), link(PageId::Reverb, "Reverb")};
const Row kGlobalRows[] = {link(PageId::Presets, "Preset"), param(P::GlbVolume), param(P::GlbOctave), param(P::GlbLed)};
const Row kDriveRows[] = {param(P::DrvMode), param(P::DrvAmount), param(P::DrvBits), param(P::DrvRate)};
const Row kModRows[] = {param(P::ModMode), param(P::ModRate), param(P::ModDepth), param(P::ModMix)};
const Row kDelayRows[] = {param(P::DlyTime), param(P::DlyFeedback), param(P::DlyMix)};
const Row kReverbRows[] = {param(P::RevSize), param(P::RevDamp), param(P::RevMix)};

struct PageDef { const char* title; const Row* rows; int count; };

template <int N>
constexpr PageDef page_def(const char* title, const Row (&rows)[N]) { return {title, rows, N}; }

// Order must match PageId; Presets is generated.
const PageDef kPages[] = {
    page_def("Oscillator", kOscRows), page_def("Filter", kFilterRows), page_def("Envelope", kEnvRows),
    page_def("Effects", kFxRows), page_def("Global", kGlobalRows),
    page_def("Effects / Drive", kDriveRows), page_def("Effects / Chorus", kModRows),
    page_def("Effects / Delay", kDelayRows), page_def("Effects / Reverb", kReverbRows),
    {"Global / Preset", nullptr, 0},
};

}  // namespace

const char* group_short_name(int group) {
    static const char* const kNames[kGroupCount] = {"OSC", "FLT", "ENV", "FX", "GLB"};
    return kNames[group];
}

const char* Menu::title() const { return kPages[static_cast<int>(page_)].title; }

int Menu::row_count() const {
    return page_ == PageId::Presets ? preset_count() : kPages[static_cast<int>(page_)].count;
}

Row Menu::row(int index) const {
    if (page_ == PageId::Presets) return {RowKind::Preset, P::OscWave, PageId::Presets, preset_name(index), index};
    return kPages[static_cast<int>(page_)].rows[index];
}

void Menu::set_cursor(int cursor) {
    const int last = row_count() - 1;
    cursor_ = cursor < 0 ? 0 : (cursor > last ? last : cursor);
    if (cursor_ < top_) top_ = cursor_;
    if (cursor_ >= top_ + kVisibleRows) top_ = cursor_ - kVisibleRows + 1;
}

void Menu::enter(PageId page) {
    parent_cursor_ = cursor_;
    parent_top_ = top_;
    page_ = page;
    top_ = 0;
    cursor_ = 0;
    if (page == PageId::Presets) set_cursor(preset_);
}

void Menu::leave() {
    if (!in_subpage()) return;
    page_ = static_cast<PageId>(group_);
    cursor_ = parent_cursor_;
    top_ = parent_top_;
}

void Menu::switch_group(int delta) {
    const int g = (static_cast<int>(group_) + delta + kGroupCount) % kGroupCount;
    group_ = static_cast<Group>(g);
    page_ = static_cast<PageId>(g);
    cursor_ = 0;
    top_ = 0;
}

void Menu::adjust(int detents, bool coarse) {
    const Row r = row(cursor_);
    if (r.kind == RowKind::Param) params_->adjust(r.param, detents, coarse);
}

void Menu::reset_value() {
    const Row r = row(cursor_);
    if (r.kind == RowKind::Param) params_->reset(r.param);
}

void Menu::select() {
    const Row r = row(cursor_);
    switch (r.kind) {
        case RowKind::Page: enter(r.target); break;
        case RowKind::Preset: load_preset(r.preset); break;
        case RowKind::Param: {
            const ParamDef& d = param_def(r.param);
            if (d.curve == Curve::Enum) {  // A cycles and wraps; encoders stop at the ends
                const int v = params_->get_int(r.param) + 1;
                params_->set(r.param, v > static_cast<int>(d.max) ? d.min : static_cast<float>(v));
            }
            break;
        }
    }
}

void Menu::load_preset(int index) {
    const int n = preset_count();
    preset_ = (index % n + n) % n;
    preset_load(preset_, *params_);
}

void Menu::handle(const MappedAction& a) {
    switch (a.action) {
        case Action::None: break;
        case Action::MenuUp: set_cursor(cursor_ - 1); break;
        case Action::MenuDown: set_cursor(cursor_ + 1); break;
        case Action::Select: select(); break;
        case Action::Back: leave(); break;
        case Action::GroupPrev: switch_group(-1); break;
        case Action::GroupNext: switch_group(1); break;
        case Action::AdjustCoarse: adjust(a.arg, true); break;
        case Action::AdjustFine: adjust(a.arg, false); break;
        case Action::ResetValue: reset_value(); break;
        case Action::OctaveUp: params_->adjust(P::GlbOctave, 1, true); break;
        case Action::OctaveDown: params_->adjust(P::GlbOctave, -1, true); break;
        case Action::PresetPrev: load_preset(preset_ - 1); break;
        case Action::PresetNext: load_preset(preset_ + 1); break;
        case Action::WaveNext: {
            const int v = params_->get_int(P::OscWave) + 1;
            params_->set(P::OscWave, v > static_cast<int>(param_def(P::OscWave).max) ? 0.0f : static_cast<float>(v));
            break;
        }
        case Action::AdjustVolume: params_->adjust(P::GlbVolume, a.arg, true); break;
        case Action::AdjustCutoff: params_->adjust(P::FltCutoff, a.arg, true); break;
        case Action::Panic: break;  // handled by App, which owns the engine
    }
}

}  // namespace danger
