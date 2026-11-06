#pragma once
#include "dsp/DspUtil.hpp"

namespace nocturne::dsp {

// Sample-accurate ADSR. Attack is linear; decay and release are exponential and
// reach 0.1 % of the remaining distance after exactly the configured time.
class Adsr {
 public:
  enum class Stage { Idle, Attack, Decay, Sustain, Release };

  void setSampleRate(double sr) { sr_ = sr; update(); }
  void setParams(float attack, float decay, float sustain, float release) {
    a_ = attack; d_ = decay; s_ = clampv(sustain, 0.0f, 1.0f); r_ = release;
    update();
  }
  void noteOn() { stage_ = Stage::Attack; }  // continues from the current level: no click
  void noteOff() {
    if (stage_ != Stage::Idle) stage_ = Stage::Release;
  }
  void reset() { stage_ = Stage::Idle; level_ = 0.0f; }

  Stage stage() const { return stage_; }
  float level() const { return level_; }
  bool active() const { return stage_ != Stage::Idle; }

  float next() {
    switch (stage_) {
      case Stage::Idle:
        level_ = 0.0f;
        break;
      case Stage::Attack:
        level_ += attackInc_;
        if (level_ >= 1.0f) {
          level_ = 1.0f;
          stage_ = Stage::Decay;
        }
        break;
      case Stage::Decay:
        level_ = s_ + (level_ - s_) * decayCoef_;
        if (level_ - s_ < 1e-3f) {
          level_ = s_;
          stage_ = Stage::Sustain;
        }
        break;
      case Stage::Sustain:
        level_ += (s_ - level_) * 0.004f;  // follows live sustain changes smoothly
        break;
      case Stage::Release:
        level_ *= releaseCoef_;
        if (level_ < 1e-3f) {
          level_ = 0.0f;
          stage_ = Stage::Idle;
        }
        break;
    }
    return level_;
  }

 private:
  void update() {
    const float sr = static_cast<float>(sr_);
    attackInc_ = 1.0f / (std::max(a_, 0.0005f) * sr);
    decayCoef_ = std::exp(-6.9078f / (std::max(d_, 0.001f) * sr));
    releaseCoef_ = std::exp(-6.9078f / (std::max(r_, 0.001f) * sr));
  }

  double sr_ = 48000.0;
  float a_ = 0.01f, d_ = 0.2f, s_ = 0.7f, r_ = 0.3f;
  float attackInc_ = 0.01f, decayCoef_ = 0.999f, releaseCoef_ = 0.999f;
  float level_ = 0.0f;
  Stage stage_ = Stage::Idle;
};

}  // namespace nocturne::dsp
