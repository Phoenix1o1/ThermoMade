#include "tm_fusion.h"

namespace tmc {

double Median3::push(double v) {
    b_[i_] = v;
    i_ = (i_ + 1) % 3;
    if (n_ < 3) ++n_;
    if (n_ == 1) return v;
    if (n_ == 2) return (b_[0] + b_[1] + b_[2] - b_[i_]) / 2.0;  // mean of the two stored values
    const double a = b_[0], b = b_[1], c = b_[2];
    if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
    if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
    return c;
}

bool Fusion::plausible(double t) const {
    return t == t && t >= p_->t_valid_min_c && t <= p_->t_valid_max_c;
}

void Fusion::restoreOffset(double offset, double learned_s) {
    if (offset == offset && offset > -10.0 && offset < 10.0 && learned_s >= 0.0) {
        offset_ = offset;
        learned_s_ = learned_s;
    }
}

void Fusion::onRoom(uint32_t now_ms, double t_c, double rh) {
    if (!plausible(t_c)) return;            // ignored: the node goes stale if this persists
    room_t_ = p_->median3 ? room_med_.push(t_c) : t_c;
    room_rx_ms_ = now_ms;
    have_room_ = true;
    room_rh_ = (rh == rh && rh >= 0.0 && rh <= 100.0) ? rh : -1.0;

    // slope estimate, updated at most every 5 minutes
    if (!slope_init_) {
        slope_init_ = true;
        slope_prev_t_ = room_t_;
        slope_prev_ms_ = now_ms;
    } else {
        const uint32_t dms = now_ms - slope_prev_ms_;
        if (dms >= 300000u) {
            const double dh = static_cast<double>(dms) / 3.6e6;
            const double inst = (room_t_ - slope_prev_t_) / dh;
            slope_c_h_ += 0.5 * (inst - slope_c_h_);
            slope_prev_t_ = room_t_;
            slope_prev_ms_ = now_ms;
        }
    }
}

void Fusion::onBase(uint32_t now_ms, bool ok, double t_c) {
    if (ok && plausible(t_c)) {
        base_t_ = p_->median3 ? base_med_.push(t_c) : t_c;
        base_rx_ms_ = now_ms;
        have_base_ = true;
        base_fail_count_ = 0;
    } else if (++base_fail_count_ >= 3) {
        have_base_ = false;
        base_med_.reset();
    }
}

const FusionOut& Fusion::update(uint32_t now_ms, bool base_disturbed) {
    const ControlParams& p = *p_;
    const uint32_t stale_ms = p.stale_s * 1000u;
    double dt = 0.0;
    if (have_last_update_) dt = static_cast<double>(now_ms - last_update_ms_) / 1000.0;
    have_last_update_ = true;
    last_update_ms_ = now_ms;

    out_.room_age_ms = have_room_ ? now_ms - room_rx_ms_ : UINT32_MAX;
    out_.room_ok = have_room_ && out_.room_age_ms <= stale_ms;
    out_.base_ok = have_base_ && (now_ms - base_rx_ms_) <= stale_ms;
    out_.t_room = room_t_;
    out_.rh_room = out_.room_ok ? room_rh_ : -1.0;
    out_.t_base_raw = base_t_;

    // offset learning (base - room), only while the room is steady and the CYD is not heating
    const double abs_slope = slope_c_h_ < 0 ? -slope_c_h_ : slope_c_h_;
    if (out_.room_ok && out_.base_ok && !base_disturbed && abs_slope <= p.offset_learn_max_slope_c_h &&
        dt > 0.0 && dt < 600.0) {
        const double diff = base_t_ - room_t_;
        if (learned_s_ <= 0.0) offset_ = diff;   // first eligible sample: start from the measured gap
        else offset_ += dt / (p.tau_offset_s + dt) * (diff - offset_);
        learned_s_ += dt;
    }
    out_.offset = offset_;
    out_.offset_learned_s = learned_s_;
    out_.offset_ready = learned_s_ >= p.offset_min_learn_s;
    out_.t_base_corr = base_t_ - offset_;

    // disagreement (only meaningful once the offset is known)
    if (out_.room_ok && out_.base_ok && out_.offset_ready) {
        double d = out_.t_room - out_.t_base_corr;
        if (d < 0) d = -d;
        if (d > p.disagree_c) disagree_acc_s_ += dt;
        else disagree_acc_s_ = 0.0;
    } else {
        disagree_acc_s_ = 0.0;
    }
    out_.disagree = disagree_acc_s_ >= static_cast<double>(p.disagree_s);

    if (out_.room_ok && out_.base_ok && out_.offset_ready && !out_.disagree) {
        out_.src = TempSource::Fused;
        out_.t_c = p.w_room * out_.t_room + (1.0 - p.w_room) * out_.t_base_corr;
    } else if (out_.room_ok) {
        out_.src = TempSource::RoomOnly;
        out_.t_c = out_.t_room;
    } else if (out_.base_ok) {
        out_.src = TempSource::BaseOnly;
        out_.t_c = out_.t_base_corr;
    } else {
        out_.src = TempSource::None;
    }
    return out_;
}

}  // namespace tmc
