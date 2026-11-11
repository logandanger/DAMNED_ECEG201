#include "core/beat/layer.hpp"

namespace danger {
namespace {

constexpr int kLoop = cfg::kLoopSteps;

bool is_seed_unmuted(const PatternNote& n) { return !n.added() && !n.muted(); }
bool is_drifted(const PatternNote& n) { return n.drifted(); }
bool is_unmuted(const PatternNote& n) { return !n.muted(); }

int forward(int from, int to) { return (to - from + kLoop) % kLoop; }

}  // namespace

void Layer::reset(uint8_t inst, uint32_t ser, uint32_t rng_seed) {
    state = LayerState::Active;
    instrument = inst;
    serial = ser;
    first_birth = 0;
    gain = 1;
    target = 1;
    rate = 0;
    count_ = seed_count_ = pitch_count_ = 0;
    pc_mask_ = 0;
    rng_.seed(rng_seed);
}

int Layer::find_at(int eff_step, int pitch) const {
    for (int i = 0; i < count_; ++i) {
        if (notes_[i].eff_step() == eff_step && notes_[i].pitch() == pitch) return i;
    }
    return -1;
}

bool Layer::occupied(int eff_step) const {
    for (int i = 0; i < count_; ++i) {
        if (!notes_[i].muted() && notes_[i].eff_step() == eff_step) return true;
    }
    return false;
}

int Layer::add_seed(const SeedNote* in, int n, uint32_t birth) {
    if (seed_count_ == 0) first_birth = birth;
    int taken = 0;
    for (int k = 0; k < n; ++k) {
        const SeedNote& s = in[k];
        int hit = -1;
        for (int i = 0; i < count_; ++i) {
            if (notes_[i].step == s.step && notes_[i].note == s.note) { hit = i; break; }
        }
        if (hit >= 0) {  // played again: the note becomes seed material at the louder velocity
            PatternNote& p = notes_[hit];
            if (s.vel > p.vel) p.vel = s.vel;
            if (p.added()) { p.flags = 0; p.birth = birth; }
            ++taken;
            continue;
        }
        if (count_ == cfg::kMaxPatternNotes) break;
        notes_[count_++] = {birth, s.step, s.note, s.vel, s.len, 0, 0, 0};
        ++taken;
    }
    refresh_stats();
    return taken;
}

void Layer::refresh_stats() {
    seed_count_ = 0;
    pitch_count_ = 0;
    pc_mask_ = 0;
    int vel = 0, len = 0;
    low_ = 127;
    high_ = 0;
    for (int i = 0; i < count_; ++i) {
        const PatternNote& p = notes_[i];
        if (p.added()) continue;
        ++seed_count_;
        vel += p.vel;
        len += p.len;
        pc_mask_ |= static_cast<uint16_t>(1u << (p.note % 12));
        if (p.note < low_) low_ = p.note;
        if (p.note > high_) high_ = p.note;
        bool known = false;
        for (int j = 0; j < pitch_count_; ++j) known = known || pitches_[j] == p.note;
        if (!known && pitch_count_ < 16) pitches_[pitch_count_++] = p.note;
    }
    if (seed_count_) {
        mean_vel_ = vel / seed_count_;
        mean_len_ = (len + seed_count_ / 2) / seed_count_;
    }
}

float Layer::note_ramp(const PatternNote& n, uint32_t now) {
    const float age = static_cast<float>(static_cast<int32_t>(now - n.birth));
    const float r = age / (cfg::kFadeInBars * cfg::kStepsPerBar);
    return r < 0 ? 0 : (r > 1 ? 1 : r);
}

float Layer::fade_in(uint32_t now) const {
    float m = 1;
    for (int i = 0; i < count_; ++i) {
        const float r = note_ramp(notes_[i], now);
        if (!notes_[i].added() && r < m) m = r;
    }
    return m;
}

int Layer::added_count() const {
    int a = 0;
    for (int i = 0; i < count_; ++i) a += notes_[i].added();
    return a;
}

int Layer::max_added() const {
    const int m = static_cast<int>(static_cast<float>(seed_count_) * cfg::kMaxAddedRatio);
    return m < 2 ? 2 : m;
}

// Random note that matches and has completed one full, unmutated pass.
int Layer::pick(bool (*pred)(const PatternNote&), uint32_t now) {
    int candidates[cfg::kMaxPatternNotes];
    int n = 0;
    for (int i = 0; i < count_; ++i) {
        if (static_cast<int32_t>(now - notes_[i].birth) >= kLoop && pred(notes_[i])) candidates[n++] = i;
    }
    return n ? candidates[rng_.below(n)] : -1;
}

void Layer::remove(int i) { notes_[i] = notes_[--count_]; }

bool Layer::op_drop(uint32_t now) {
    int unmuted = 0;
    for (int i = 0; i < count_; ++i) unmuted += is_seed_unmuted(notes_[i]);
    const int keep = seed_count_ / 2 > 2 ? seed_count_ / 2 : 2;
    if (unmuted <= keep) return false;
    const int i = pick(is_seed_unmuted, now);
    if (i < 0) return false;
    notes_[i].flags |= PatternNote::kMuted;
    return true;
}

bool Layer::op_restore(uint32_t now) {
    const int i = pick(is_drifted, now);
    if (i < 0) return false;
    if (notes_[i].added()) {
        remove(i);
    } else {
        notes_[i].flags = 0;
        notes_[i].nudge = 0;
        notes_[i].octave = 0;
    }
    return true;
}

bool Layer::op_add(uint32_t now) {
    if (pitch_count_ == 0 || added_count() >= max_added() || count_ == cfg::kMaxPatternNotes) return false;
    // An 8th-note position this layer leaves empty, with one of the pitches that were played.
    int free_steps[kLoop / 2];
    int n = 0;
    for (int s = 0; s < kLoop; s += 2) {
        if (!occupied(s)) free_steps[n++] = s;
    }
    if (!n) return false;
    const int step = free_steps[rng_.below(n)];
    const int pitch = pitches_[rng_.below(pitch_count_)];
    const int vel = static_cast<int>(static_cast<float>(mean_vel_) * cfg::kAddedVelocity);
    notes_[count_++] = {now, static_cast<uint8_t>(step), static_cast<uint8_t>(pitch), static_cast<uint8_t>(vel),
                        static_cast<uint8_t>(mean_len_), 0, 0, PatternNote::kAdded};
    return true;
}

bool Layer::op_octave(uint32_t now) {
    const int i = pick(is_unmuted, now);
    if (i < 0) return false;
    PatternNote& p = notes_[i];
    const int dir = p.octave != 0 ? -p.octave : (rng_.chance(0.5f) ? 1 : -1);
    const int pitch = p.pitch() + 12 * dir;
    if (pitch < low_ - 12 || pitch > high_ + 12 || pitch < 0 || pitch > 127) return false;
    if (find_at(p.eff_step(), pitch) >= 0) return false;
    p.octave = static_cast<int8_t>(p.octave + dir);
    return true;
}

bool Layer::op_nudge(uint32_t now) {
    const int i = pick(is_unmuted, now);
    if (i < 0) return false;
    PatternNote& p = notes_[i];
    const int d = rng_.chance(0.5f) ? 1 : -1;
    const int nudge = p.nudge + d;
    if (nudge > cfg::kMaxNudgeSteps || nudge < -cfg::kMaxNudgeSteps) return false;
    const int target = (p.step + nudge + kLoop) % kLoop;
    if (find_at(target, p.pitch()) >= 0) return false;
    p.nudge = static_cast<int8_t>(nudge);
    return true;
}

// A pitch from the played set, between a note and the next one, a 16th before the second.
bool Layer::op_passing(uint32_t now) {
    if (added_count() >= max_added() || count_ == cfg::kMaxPatternNotes) return false;
    const int a = pick(is_unmuted, now);
    if (a < 0) return false;
    const PatternNote& na = notes_[a];
    int b = -1, best = kLoop;
    for (int i = 0; i < count_; ++i) {
        if (notes_[i].muted() || i == a) continue;
        const int d = forward(na.eff_step(), notes_[i].eff_step());
        if (d >= 2 && d < best) { best = d; b = i; }
    }
    if (b < 0) return false;
    const int pa = na.pitch(), pb = notes_[b].pitch();
    if (pa - pb < 3 && pb - pa < 3) return false;
    const int lo = pa < pb ? pa : pb, hi = pa < pb ? pb : pa;
    const int mid2 = lo + hi;
    int pitch = -1;
    for (int p = lo + 1; p < hi; ++p) {
        if (!(pc_mask_ & (1u << (p % 12)))) continue;
        if (pitch < 0 || (2 * p - mid2) * (2 * p - mid2) < (2 * pitch - mid2) * (2 * pitch - mid2)) pitch = p;
    }
    if (pitch < 0) return false;
    const int step = (notes_[b].eff_step() - 1 + kLoop) % kLoop;
    if (occupied(step)) return false;
    const int vel = static_cast<int>(static_cast<float>(mean_vel_) * cfg::kAddedVelocity * 0.85f);
    notes_[count_++] = {now, static_cast<uint8_t>(step), static_cast<uint8_t>(pitch), static_cast<uint8_t>(vel), 1, 0, 0,
                        PatternNote::kAdded};
    return true;
}

void Layer::mutate(uint32_t now, float intensity) {
    if (intensity <= 0 || seed_count_ == 0) return;
    const float expected = cfg::kMutationRate * intensity;
    int n = static_cast<int>(expected);
    if (rng_.chance(expected - static_cast<float>(n))) ++n;

    for (int k = 0; k < n; ++k) {
        int drift = 0;
        for (int i = 0; i < count_; ++i) drift += notes_[i].drifted();
        // The more the loop has drifted, the more likely it pulls back toward the seed.
        const float w[6] = {1.0f, 1.0f, 0.6f, 0.8f, 0.8f,
                            0.3f + 2.0f * static_cast<float>(drift) / static_cast<float>(seed_count_)};
        float total = 0;
        for (float x : w) total += x;
        float r = rng_.uniform() * total;
        int op = 0;
        while (op < 5 && r >= w[op]) r -= w[op++];
        switch (op) {
            case 0: op_drop(now); break;
            case 1: op_add(now); break;
            case 2: op_octave(now); break;
            case 3: op_nudge(now); break;
            case 4: op_passing(now); break;
            default: op_restore(now); break;
        }
    }
}

}  // namespace danger
