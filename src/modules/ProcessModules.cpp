#include <cmath>

#include "dsp/Adsr.hpp"
#include "dsp/Filter.hpp"
#include "modules/Module.hpp"
#include "modules/ModuleRegistry.hpp"

namespace nocturne {
namespace {

// --------------------------------------------------------------- FILTER
class FilterModule final : public Module {
 public:
  enum Param { Type, Cutoff, Res, Drive, KeyTrack };
  enum In { InAudio, InCutoff, InRes };

  void prepare(double sr) override {
    Module::prepare(sr);
    svf_.setSampleRate(sr);
    for (auto* s : {&cutoff_, &res_, &drive_}) {
      s->setSampleRate(sr);
      s->setTime(0.008f);
    }
  }
  void reset() override {
    svf_.reset();
    primed_ = false;
  }

  void process(const ModuleIO& io) override {
    if (!primed_) {
      cutoff_.snap(std::log2(param(Cutoff)));
      res_.snap(param(Res));
      drive_.snap(param(Drive));
      primed_ = true;
    }
    // Cutoff is smoothed in the octave domain so sweeps are perceptually even.
    cutoff_.setTarget(std::log2(param(Cutoff)));
    res_.setTarget(param(Res));
    drive_.setTarget(param(Drive));
    const auto type = static_cast<dsp::FilterType>(static_cast<int>(param(Type)));
    const float track = io.voice ? param(KeyTrack) * io.voice->pitch : 0.0f;
    const float* in = io.in[InAudio];
    const float* cutIn = io.in[InCutoff];
    const float* resIn = io.in[InRes];

    for (int i = 0; i < io.n; ++i) {
      float oct = cutoff_.next() + track;
      if (cutIn) oct += 5.0f * cutIn[i];
      float res = res_.next();
      if (resIn) res += 0.5f * resIn[i];
      const float drive = drive_.next();
      const float x = in ? in[i] : 0.0f;
      io.out[0][i] = svf_.process(x, type, std::exp2(oct), res, drive);
    }
  }

 private:
  dsp::StateVariableFilter svf_;
  dsp::Smoothed cutoff_, res_, drive_;
  bool primed_ = false;
};

// ----------------------------------------------------------------- ADSR
class AdsrModule final : public Module {
 public:
  enum Param { Attack, Decay, Sustain, Release, VelSens };

  void prepare(double sr) override {
    Module::prepare(sr);
    env_.setSampleRate(sr);
  }
  void reset() override {
    env_.reset();
    prevGate_ = false;
    pendingTrigger_ = false;
  }
  void trigger(const VoiceCtx&) override { pendingTrigger_ = true; }
  bool busy() const override { return env_.active(); }

  void process(const ModuleIO& io) override {
    env_.setParams(param(Attack), param(Decay), param(Sustain), param(Release));
    const float vs = param(VelSens);
    const float velGain = io.voice ? 1.0f - vs + vs * io.voice->velocity : 1.0f;
    const float* gateIn = io.in[0];

    if (!gateIn) {
      // No gate patched: follow the keyboard gate of this voice.
      const bool gate = io.voice && io.voice->gate > 0.5f;
      if (pendingTrigger_ && gate) { env_.noteOn(); prevGate_ = true; }
      pendingTrigger_ = false;
      if (gate && !prevGate_) env_.noteOn();
      if (!gate && prevGate_) env_.noteOff();
      prevGate_ = gate;
      for (int i = 0; i < io.n; ++i) io.out[0][i] = env_.next() * velGain;
    } else {
      pendingTrigger_ = false;
      for (int i = 0; i < io.n; ++i) {
        const bool gate = prevGate_ ? gateIn[i] > 0.3f : gateIn[i] > 0.6f;  // hysteresis
        if (gate && !prevGate_) env_.noteOn();
        if (!gate && prevGate_) env_.noteOff();
        prevGate_ = gate;
        io.out[0][i] = env_.next() * velGain;
      }
    }
  }

 private:
  dsp::Adsr env_;
  bool prevGate_ = false;
  bool pendingTrigger_ = false;
};

// ------------------------------------------------------------------ AMP
class AmpModule final : public Module {
 public:
  enum Param { Gain };
  void prepare(double sr) override {
    Module::prepare(sr);
    gain_.setSampleRate(sr);
    gain_.setTime(0.005f);
  }
  void reset() override { primed_ = false; }
  void process(const ModuleIO& io) override {
    if (!primed_) { gain_.snap(param(Gain)); primed_ = true; }
    gain_.setTarget(param(Gain));
    const float* in = io.in[0];
    const float* cv = io.in[1];
    for (int i = 0; i < io.n; ++i) {
      float g = gain_.next();
      if (cv) g *= std::max(cv[i], 0.0f);
      io.out[0][i] = in ? in[i] * g : 0.0f;
    }
  }

 private:
  dsp::Smoothed gain_;
  bool primed_ = false;
};

// ---------------------------------------------------------------- MIXER
class MixerModule final : public Module {
 public:
  void prepare(double sr) override {
    Module::prepare(sr);
    for (auto& g : gains_) { g.setSampleRate(sr); g.setTime(0.005f); }
  }
  void reset() override { primed_ = false; }
  void process(const ModuleIO& io) override {
    if (!primed_) {
      for (int c = 0; c < 4; ++c) gains_[static_cast<size_t>(c)].snap(param(c));
      primed_ = true;
    }
    for (int c = 0; c < 4; ++c) gains_[static_cast<size_t>(c)].setTarget(param(c));
    for (int i = 0; i < io.n; ++i) {
      float sum = 0.0f;
      for (int c = 0; c < 4; ++c) {
        const float g = gains_[static_cast<size_t>(c)].next();
        if (io.in[c]) sum += io.in[c][i] * g;
      }
      io.out[0][i] = sum;
    }
  }

 private:
  dsp::Smoothed gains_[4];
  bool primed_ = false;
};

}  // namespace

void registerProcessModules(std::vector<ModuleSpec>& out) {
  {
    ModuleSpec s;
    s.type = "filter"; s.label = "FILTER"; s.prefix = "FILTER"; s.scope = Scope::Voice;
    s.inputs = {audioIn("IN"), cvIn("CUTOFF", "5 octaves per unit", true), cvIn("RES", "resonance CV")};
    s.outputs = {audioOut("OUT")};
    s.params = {
        enumParam("type", "TYPE", {"LOWPASS", "HIGHPASS", "BANDPASS"}, 0),
        floatParam("cutoff", "CUTOFF", 20.0f, 20000.0f, 2400.0f, "Hz", ParamScale::Log),
        floatParam("res", "RES", 0.0f, 1.0f, 0.2f),
        floatParam("drive", "DRIVE", 0.0f, 1.0f, 0.0f),
        floatParam("keytrack", "KEYTRK", 0.0f, 1.0f, 0.0f),
    };
    s.create = [] { return std::unique_ptr<Module>(new FilterModule()); };
    out.push_back(std::move(s));
  }
  {
    ModuleSpec s;
    s.type = "adsr"; s.label = "ENVELOPE"; s.prefix = "ADSR"; s.scope = Scope::Voice;
    s.isEnvelope = true;
    s.inputs = {cvIn("GATE", "unpatched = keyboard gate")};
    s.outputs = {cvOut("OUT")};
    s.params = {
        floatParam("attack", "A", 0.001f, 10.0f, 0.01f, "s", ParamScale::Log),
        floatParam("decay", "D", 0.005f, 10.0f, 0.25f, "s", ParamScale::Log),
        floatParam("sustain", "S", 0.0f, 1.0f, 0.7f),
        floatParam("release", "R", 0.005f, 15.0f, 0.4f, "s", ParamScale::Log),
        floatParam("velocity", "VEL", 0.0f, 1.0f, 0.5f),
    };
    s.create = [] { return std::unique_ptr<Module>(new AdsrModule()); };
    out.push_back(std::move(s));
  }
  {
    ModuleSpec s;
    s.type = "amp"; s.label = "AMPLIFIER"; s.prefix = "AMP"; s.scope = Scope::Voice;
    s.inputs = {audioIn("IN"), cvIn("GAIN", "multiplies the gain", true)};
    s.outputs = {audioOut("OUT")};
    s.params = {floatParam("gain", "GAIN", 0.0f, 1.0f, 0.8f)};
    s.create = [] { return std::unique_ptr<Module>(new AmpModule()); };
    out.push_back(std::move(s));
  }
  {
    ModuleSpec s;
    s.type = "mixer"; s.label = "MIXER"; s.prefix = "MIX"; s.scope = Scope::Voice;
    s.inputs = {audioIn("IN1"), audioIn("IN2"), audioIn("IN3"), audioIn("IN4")};
    s.outputs = {audioOut("OUT")};
    s.params = {floatParam("level1", "LVL 1", 0.0f, 1.0f, 0.7f), floatParam("level2", "LVL 2", 0.0f, 1.0f, 0.7f),
                floatParam("level3", "LVL 3", 0.0f, 1.0f, 0.7f), floatParam("level4", "LVL 4", 0.0f, 1.0f, 0.7f)};
    s.create = [] { return std::unique_ptr<Module>(new MixerModule()); };
    out.push_back(std::move(s));
  }
}

}  // namespace nocturne
