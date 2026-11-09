// TermoMade control library — sensor validation and fusion (docs/04 §1).
//  * each sensor: plausibility window + median-of-3 (rejects single spikes, follows real steps)
//  * room node: stale after STALE_S without a frame
//  * CYD (base) sensor: its offset vs the room node is learned slowly while the room is steady
//    and the CYD is not self-heating (screen off); it joins the fusion once learned
//  * disagreement alarm: |room - base_corrected| > DISAGREE for DISAGREE_S -> room only
#pragma once
#include <cstdint>
#include "tm_params.h"
#include "tm_types.h"

namespace tmc {

class Median3 {
public:
    void reset() { n_ = 0; i_ = 0; b_[0] = b_[1] = b_[2] = 0.0; }
    double push(double v);
    int count() const { return n_; }
private:
    double b_[3] = {0, 0, 0};
    int n_ = 0, i_ = 0;
};

struct FusionOut {
    TempSource src = TempSource::None;
    double t_c = 0.0;            // control temperature (valid if src != None)
    bool room_ok = false;
    bool base_ok = false;
    double t_room = 0.0;         // filtered room node temperature
    double rh_room = -1.0;       // room humidity, -1 if unknown
    double t_base_raw = 0.0;     // filtered CYD sensor temperature
    double t_base_corr = 0.0;    // corrected by the learned offset
    double offset = 0.0;         // learned base - room
    double offset_learned_s = 0.0;
    bool offset_ready = false;
    bool disagree = false;
    uint32_t room_age_ms = UINT32_MAX;
};

class Fusion {
public:
    explicit Fusion(const ControlParams& p) : p_(&p) {}
    void setParams(const ControlParams& p) { p_ = &p; }

    // Restore the learned offset after a reboot.
    void restoreOffset(double offset, double learned_s);
    // Forget the learned offset (e.g. the CYD sensor was moved). The base sensor leaves the fusion
    // until it has been learned again.
    void resetOffset() { offset_ = 0.0; learned_s_ = 0.0; disagree_acc_s_ = 0.0; }

    // New room-node frame. rh < 0 = no humidity sensor.
    void onRoom(uint32_t now_ms, double t_c, double rh);
    // New CYD sensor reading (ok=false on read error).
    void onBase(uint32_t now_ms, bool ok, double t_c);

    // Recompute. base_disturbed = CYD screen on (self-heating) -> no offset learning.
    const FusionOut& update(uint32_t now_ms, bool base_disturbed);

    const FusionOut& out() const { return out_; }
    double offset() const { return offset_; }
    double offsetLearnedS() const { return learned_s_; }

private:
    bool plausible(double t) const;

    const ControlParams* p_;
    FusionOut out_;
    Median3 room_med_, base_med_;
    bool have_room_ = false, have_base_ = false;
    uint32_t room_rx_ms_ = 0, base_rx_ms_ = 0;
    uint32_t base_fail_count_ = 0;
    double room_t_ = 0, base_t_ = 0, room_rh_ = -1;
    // room slope estimate (degC/h)
    bool slope_init_ = false;
    double slope_prev_t_ = 0;
    uint32_t slope_prev_ms_ = 0;
    double slope_c_h_ = 0;
    // offset learning
    double offset_ = 0.0;
    double learned_s_ = 0.0;
    bool have_last_update_ = false;
    uint32_t last_update_ms_ = 0;
    double disagree_acc_s_ = 0.0;
};

}  // namespace tmc
