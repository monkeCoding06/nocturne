#include <cmath>

#include "dsp/Effects.hpp"
#include "modules/Module.hpp"
#include "modules/ModuleRegistry.hpp"

namespace nocturne {
namespace {

// ---------------------------------------------------------------- DRIVE
class DriveModule final : public Module {
 public:
  enum Param { Drive, Tone, Mix, Level };
  void prepare(double sr) override {
    Module::prepare(sr);
    sat_.prepare(sr);
    for (auto* s : {&drive_, &mix_, &level_}) { s->setSampleRate(sr); s->setTime(0.01f); }
  }
  void reset() override { sat_.reset(); primed_ = false; }
  void process(const ModuleIO& io) override {
    if (!primed_) {
      drive_.snap(param(Drive)); mix_.snap(param(Mix)); level_.snap(param(Level));
      primed_ = true;
    }
    drive_.setTarget(param(Drive)); mix_.setTarget(param(Mix)); level_.setTarget(param(Level));
    const float tone = param(Tone);
    for (int i = 0; i < io.n; ++i) {
      const float x = io.in[0] ? io.in[0][i] : 0.0f;
      io.out[0][i] = sat_.process(x, drive_.next(), tone, mix_.next(), level_.next());
    }
  }

 private:
  dsp::Saturator sat_;
  dsp::Smoothed drive_, mix_, level_;
  bool primed_ = false;
};

// ---------------------------------------------------------------- DELAY
class DelayModule final : public Module {
 public:
  enum Param { Time, Feedback, Damp, Mix };
  void prepare(double sr) override {
    Module::prepare(sr);
    delay_.prepare(sr, 2.0f);
    time_.setSampleRate(sr); time_.setTime(0.12f);
    fb_.setSampleRate(sr); fb_.setTime(0.02f);
    mix_.setSampleRate(sr); mix_.setTime(0.02f);
  }
  void reset() override { delay_.reset(); primed_ = false; }
  void process(const ModuleIO& io) override {
    const float sr = static_cast<float>(sr_);
    if (!primed_) {
      time_.snap(param(Time) * 0.001f * sr); fb_.snap(param(Feedback)); mix_.snap(param(Mix));
      primed_ = true;
    }
    time_.setTarget(param(Time) * 0.001f * sr);
    fb_.setTarget(param(Feedback));
    mix_.setTarget(param(Mix));
    const float damp = param(Damp);
    for (int i = 0; i < io.n; ++i) {
      const float x = io.in[0] ? io.in[0][i] : 0.0f;
      io.out[0][i] = delay_.process(x, time_.next(), fb_.next(), damp, mix_.next());
    }
  }

 private:
  dsp::Delay delay_;
  dsp::Smoothed time_, fb_, mix_;
  bool primed_ = false;
};

// --------------------------------------------------------------- REVERB
class ReverbModule final : public Module {
 public:
  enum Param { Size, Damp, Mix };
  void prepare(double sr) override {
    Module::prepare(sr);
    rev_.prepare(sr);
    mix_.setSampleRate(sr); mix_.setTime(0.02f);
  }
  void reset() override { rev_.reset(); primed_ = false; }
  void process(const ModuleIO& io) override {
    if (!primed_) { mix_.snap(param(Mix)); primed_ = true; }
    mix_.setTarget(param(Mix));
    for (int i = 0; i < io.n; ++i) {
      const float x = io.in[0] ? io.in[0][i] : 0.0f;
      float wl, wr;
      rev_.process(x, param(Size), param(Damp), wl, wr);
      const float m = mix_.next();
      io.out[0][i] = x * (1.0f - m) + wl * m;
      io.out[1][i] = x * (1.0f - m) + wr * m;
    }
  }

 private:
  dsp::Reverb rev_;
  dsp::Smoothed mix_;
  bool primed_ = false;
};

// --------------------------------------------------------------- MASTER
// Sums everything patched to IN / L / R, applies level and a soft limiter.
class MasterModule final : public Module {
 public:
  void prepare(double sr) override {
    Module::prepare(sr);
    level_.setSampleRate(sr);
    level_.setTime(0.01f);
  }
  void reset() override { primed_ = false; }
  void process(const ModuleIO& io) override {
    if (!primed_) { level_.snap(param(0)); primed_ = true; }
    level_.setTarget(param(0));
    for (int i = 0; i < io.n; ++i) {
      const float c = io.in[0] ? io.in[0][i] : 0.0f;
      const float l = c + (io.in[1] ? io.in[1][i] : 0.0f);
      const float r = c + (io.in[2] ? io.in[2][i] : 0.0f);
      const float g = level_.next();
      io.out[0][i] = dsp::softLimit(l * g);
      io.out[1][i] = dsp::softLimit(r * g);
    }
  }

 private:
  dsp::Smoothed level_;
  bool primed_ = false;
};

}  // namespace

void registerEffectModules(std::vector<ModuleSpec>& out) {
  {
    ModuleSpec s;
    s.type = "drive"; s.label = "DISTORTION"; s.prefix = "DRIVE"; s.scope = Scope::Global;
    s.inputs = {audioIn("IN")};
    s.outputs = {audioOut("OUT")};
    s.params = {
        floatParam("drive", "DRIVE", 0.0f, 1.0f, 0.4f),
        floatParam("tone", "TONE", 200.0f, 16000.0f, 6000.0f, "Hz", ParamScale::Log),
        floatParam("mix", "MIX", 0.0f, 1.0f, 1.0f),
        floatParam("level", "LEVEL", 0.0f, 1.5f, 0.8f),
    };
    s.create = [] { return std::unique_ptr<Module>(new DriveModule()); };
    out.push_back(std::move(s));
  }
  {
    ModuleSpec s;
    s.type = "delay"; s.label = "DELAY"; s.prefix = "DELAY"; s.scope = Scope::Global;
    s.inputs = {audioIn("IN")};
    s.outputs = {audioOut("OUT")};
    s.params = {
        floatParam("time", "TIME", 5.0f, 1500.0f, 350.0f, "ms", ParamScale::Log),
        floatParam("feedback", "FEEDBACK", 0.0f, 0.95f, 0.4f),
        floatParam("damp", "DAMP", 500.0f, 16000.0f, 5000.0f, "Hz", ParamScale::Log),
        floatParam("mix", "MIX", 0.0f, 1.0f, 0.3f),
    };
    s.create = [] { return std::unique_ptr<Module>(new DelayModule()); };
    out.push_back(std::move(s));
  }
  {
    ModuleSpec s;
    s.type = "reverb"; s.label = "REVERB"; s.prefix = "REVERB"; s.scope = Scope::Global;
    s.inputs = {audioIn("IN")};
    s.outputs = {audioOut("L"), audioOut("R")};
    s.params = {
        floatParam("size", "SIZE", 0.0f, 1.0f, 0.6f),
        floatParam("damp", "DAMP", 0.0f, 1.0f, 0.5f),
        floatParam("mix", "MIX", 0.0f, 1.0f, 0.3f),
    };
    s.create = [] { return std::unique_ptr<Module>(new ReverbModule()); };
    out.push_back(std::move(s));
  }
  {
    ModuleSpec s;
    s.type = "master"; s.label = "MASTER"; s.prefix = "MASTER"; s.scope = Scope::Global;
    s.unique = true;
    s.inputs = {audioIn("IN", "mono / center"), audioIn("L", "left only"), audioIn("R", "right only")};
    PortSpec l = audioOut("L"), r = audioOut("R");
    l.internal = r.internal = true;
    s.outputs = {l, r};
    s.params = {floatParam("level", "LEVEL", 0.0f, 1.0f, 0.7f)};
    s.create = [] { return std::unique_ptr<Module>(new MasterModule()); };
    out.push_back(std::move(s));
  }
}

}  // namespace nocturne
