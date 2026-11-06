#include <atomic>
#include <cmath>

#include "dsp/Lfo.hpp"
#include "dsp/Oscillator.hpp"
#include "modules/Module.hpp"
#include "modules/ModuleRegistry.hpp"

namespace nocturne {
namespace {

constexpr float kMiddleC = 261.6256f;

std::uint32_t nextSeed() {
  static std::atomic<std::uint32_t> counter{0x9E3779B9u};
  return counter.fetch_add(0x6C8E9CF5u) | 1u;
}

// ------------------------------------------------------------------ OSC
class OscModule final : public Module {
 public:
  enum Param { Wave, Key, Oct, Semi, Fine, Freq, Level, Pw, Phase };
  enum In { InPitch, InFm, InPwm };

  OscModule() { osc_.seedNoise(nextSeed()); }

  void prepare(double sr) override {
    Module::prepare(sr);
    level_.setSampleRate(sr);
    level_.setTime(0.005f);
    pw_.setSampleRate(sr);
    pw_.setTime(0.005f);
  }
  void reset() override {
    osc_.reset(0.0f);
    primed_ = false;
  }

  void process(const ModuleIO& io) override {
    if (!primed_) {
      level_.snap(param(Level));
      pw_.snap(param(Pw));
      primed_ = true;
    }
    level_.setTarget(param(Level));
    pw_.setTarget(param(Pw));

    const auto wave = static_cast<dsp::Wave>(static_cast<int>(param(Wave)));
    const bool keyed = param(Key) > 0.5f && io.voice;
    const float tuning = param(Oct) + param(Semi) / 12.0f + param(Fine) / 1200.0f;
    const float base = keyed ? kMiddleC * std::exp2(io.voice->pitch + tuning)
                             : param(Freq) * std::exp2(tuning);
    const float phaseOff = param(Phase) / 360.0f;
    const float* pitchIn = io.in[InPitch];
    const float* fmIn = io.in[InFm];
    const float* pwmIn = io.in[InPwm];
    const float invSr = 1.0f / static_cast<float>(sr_);

    for (int i = 0; i < io.n; ++i) {
      float f = base;
      float mod = 0.0f;
      if (pitchIn) mod += pitchIn[i];
      if (fmIn) mod += fmIn[i];
      if (mod != 0.0f) f *= std::exp2(mod);
      float pw = pw_.next();
      if (pwmIn) pw += 0.45f * pwmIn[i];
      io.out[0][i] = osc_.next(wave, f * invSr, pw, phaseOff) * level_.next();
    }
  }

 private:
  dsp::Oscillator osc_;
  dsp::Smoothed level_, pw_;
  bool primed_ = false;
};

// ------------------------------------------------------------------ LFO
class LfoModule final : public Module {
 public:
  enum Param { Wave, Rate, Depth, Polarity, Reset, Phase, Delay, Fade };

  void prepare(double sr) override {
    Module::prepare(sr);
    lfo_.setSampleRate(sr);
    lfo_.seed(nextSeed());
    depth_.setSampleRate(sr);
    depth_.setTime(0.01f);
    rate_.setSampleRate(sr);
    rate_.setTime(0.02f);
  }
  void reset() override {
    primed_ = false;
    elapsed_ = 1e9;  // no delay/fade until a note starts
    lfo_.reset(param(Reset) > 0.5f ? 0.0f : noise_.next() * 0.5f + 0.5f);
  }
  void trigger(const VoiceCtx&) override {
    elapsed_ = 0.0;  // delay and fade-in restart with every note
    if (param(Reset) > 0.5f) lfo_.reset(0.0f);
  }

  void process(const ModuleIO& io) override {
    if (!primed_) {
      depth_.snap(param(Depth));
      rate_.snap(param(Rate));
      primed_ = true;
    }
    depth_.setTarget(param(Depth));
    rate_.setTarget(param(Rate));
    const auto wave = static_cast<dsp::LfoWave>(static_cast<int>(param(Wave)));
    const bool unipolar = param(Polarity) > 0.5f;
    const float phase = param(Phase) / 360.0f;
    const double delay = param(Delay), fade = param(Fade);
    const double dtSec = 1.0 / sr_;
    const float* rateIn = io.in[0];
    for (int i = 0; i < io.n; ++i) {
      float r = rate_.next();
      if (rateIn) r *= std::exp2(rateIn[i]);
      float v = lfo_.next(wave, r, phase);
      if (unipolar) v = 0.5f + 0.5f * v;
      float env = 1.0f;
      if (elapsed_ < delay + fade) {
        env = elapsed_ < delay ? 0.0f : (fade > 0.0 ? static_cast<float>((elapsed_ - delay) / fade) : 1.0f);
        elapsed_ += dtSec;
      }
      io.out[0][i] = v * depth_.next() * env;
    }
  }

 private:
  dsp::Lfo lfo_;
  dsp::Smoothed depth_, rate_;
  dsp::Noise noise_{nextSeed()};
  double elapsed_ = 1e9;
  bool primed_ = false;
};

// ----------------------------------------------------------------- KEYS
class KeysModule final : public Module {
 public:
  void trigger(const VoiceCtx&) override { dip_ = 2; }  // brief gate dip so envelopes retrigger
  void process(const ModuleIO& io) override {
    for (int i = 0; i < io.n; ++i) {
      io.out[0][i] = io.voice->pitch;
      float g = io.voice->gate;
      if (dip_ > 0) { g = 0.0f; --dip_; }
      io.out[1][i] = g;
      io.out[2][i] = io.voice->velocity;
    }
  }

 private:
  int dip_ = 0;
};

}  // namespace

void registerSourceModules(std::vector<ModuleSpec>& out) {
  {
    ModuleSpec s;
    s.type = "osc"; s.label = "OSCILLATOR"; s.prefix = "OSC"; s.scope = Scope::Voice;
    s.inputs = {cvIn("PITCH", "1 V/oct pitch CV"), cvIn("FM", "exponential FM (audio rate ok)", true),
                cvIn("PWM", "pulse width modulation", true)};
    s.outputs = {audioOut("OUT")};
    s.params = {
        enumParam("wave", "WAVE", {"SINE", "TRI", "SAW", "SQUARE", "PULSE", "NOISE"}, 2),
        enumParam("key", "KEY", {"OFF", "ON"}, 1),
        intParam("oct", "OCT", -4, 4, 0),
        intParam("semi", "SEMI", -24, 24, 0, "st"),
        floatParam("fine", "FINE", -100.0f, 100.0f, 0.0f, "ct", ParamScale::Linear, 1),
        floatParam("freq", "FREQ", 0.05f, 8000.0f, 261.6256f, "Hz", ParamScale::Log),
        floatParam("level", "LEVEL", 0.0f, 1.0f, 0.8f),
        floatParam("pw", "PW", 0.05f, 0.95f, 0.5f),
        floatParam("phase", "PHASE", 0.0f, 360.0f, 0.0f, "deg", ParamScale::Linear, 0),
    };
    s.create = [] { return std::unique_ptr<Module>(new OscModule()); };
    out.push_back(std::move(s));
  }
  {
    ModuleSpec s;
    s.type = "lfo"; s.label = "LFO"; s.prefix = "LFO"; s.scope = Scope::Voice;
    s.inputs = {cvIn("RATE", "1 V/oct rate CV")};
    s.outputs = {cvOut("OUT")};
    s.params = {
        enumParam("wave", "WAVE", {"SINE", "TRI", "SQUARE", "SAW", "RSAW", "S&H"}, 0),
        floatParam("rate", "RATE", 0.01f, 100.0f, 2.5f, "Hz", ParamScale::Log),
        floatParam("depth", "DEPTH", 0.0f, 1.0f, 0.5f),
        enumParam("polarity", "POLARITY", {"BIPOLAR", "UNIPOLAR"}, 0),
        enumParam("reset", "RESET", {"FREE", "KEY"}, 1),
        floatParam("phase", "PHASE", 0.0f, 360.0f, 0.0f, "deg", ParamScale::Linear, 0),
        floatParam("delay", "DELAY", 0.0f, 5.0f, 0.0f, "s"),
        floatParam("fade", "FADE IN", 0.0f, 5.0f, 0.0f, "s"),
    };
    s.create = [] { return std::unique_ptr<Module>(new LfoModule()); };
    out.push_back(std::move(s));
  }
  {
    ModuleSpec s;
    s.type = "keys"; s.label = "KEYBOARD"; s.prefix = "KEYS"; s.scope = Scope::Voice;
    s.outputs = {cvOut("PITCH", "1 V/oct, 0 = middle C"), cvOut("GATE", "high while note held"),
                 cvOut("VEL", "note velocity 0..1")};
    s.create = [] { return std::unique_ptr<Module>(new KeysModule()); };
    out.push_back(std::move(s));
  }
}

}  // namespace nocturne
