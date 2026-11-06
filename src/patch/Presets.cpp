#include "patch/Presets.hpp"

#include <cassert>
#include <stdexcept>

namespace nocturne {
namespace {

// Small builder around the public Patch API. Throws on programmer error so a
// broken factory patch is caught by the tests immediately.
struct B {
  Patch p = makeEmptyPatch();

  B(const char* name, const char* desc) {
    p.meta.name = name;
    p.meta.author = "NOCTURNE";
    p.meta.description = desc;
  }

  std::string add(const char* type) {
    std::string n;
    const std::string e = addModule(p, type, &n);
    if (!e.empty()) throw std::runtime_error(e);
    return n;
  }
  // Set a parameter by id. Enums are given as their choice index.
  B& set(const std::string& mod, const char* param, float v) {
    const int mi = p.findModule(mod);
    if (mi < 0) throw std::runtime_error("no module " + mod);
    const ModuleSpec* spec = p.specOf(mi);
    const int pi = spec->paramIndex(param);
    if (pi < 0) throw std::runtime_error("no param " + std::string(param));
    p.modules[static_cast<size_t>(mi)].params[static_cast<size_t>(pi)] = spec->params[static_cast<size_t>(pi)].clamp(v);
    return *this;
  }
  B& wire(const char* from, const char* fp, const char* to, const char* tp, float amt = 1.0f) {
    const std::string e = connect(p, from, fp, to, tp, amt);
    if (!e.empty()) throw std::runtime_error(std::string(from) + "." + fp + " -> " + to + "." + tp + ": " + e);
    return *this;
  }
  B& voices(int n, VoiceMode m = VoiceMode::Poly, float glide = 0.0f, bool legato = true) {
    p.global.voices = n; p.global.mode = m; p.global.glide = glide; p.global.legato = legato;
    return *this;
  }
};

enum { SINE, TRI, SAW, SQUARE, PULSE, NOISE };

Patch initPatch() {
  B b("INIT", "Plain sine synthesizer: OSC > AMP (ADSR) > MASTER");
  b.add("osc"); b.add("adsr"); b.add("amp");
  b.set("OSC1", "wave", SINE);
  b.set("ADSR1", "attack", 0.01f).set("ADSR1", "decay", 0.3f).set("ADSR1", "sustain", 0.7f).set("ADSR1", "release", 0.4f);
  b.wire("OSC1", "OUT", "AMP1", "IN").wire("ADSR1", "OUT", "AMP1", "GAIN").wire("AMP1", "OUT", "MASTER", "IN");
  return b.p;
}

Patch subPatch() {
  B b("SUB", "Deep bass: saw + sub sine through a closing low-pass");
  b.add("osc"); b.add("osc"); b.add("mixer"); b.add("filter"); b.add("adsr"); b.add("adsr"); b.add("amp"); b.add("drive");
  b.set("OSC1", "wave", SAW).set("OSC1", "level", 0.8f);
  b.set("OSC2", "wave", SINE).set("OSC2", "oct", -1).set("OSC2", "level", 1.0f);
  b.set("MIX1", "level1", 0.6f).set("MIX1", "level2", 0.9f);
  b.set("FILTER1", "cutoff", 220.0f).set("FILTER1", "res", 0.35f).set("FILTER1", "drive", 0.25f);
  b.set("ADSR1", "attack", 0.005f).set("ADSR1", "decay", 0.25f).set("ADSR1", "sustain", 0.85f).set("ADSR1", "release", 0.18f);
  b.set("ADSR2", "attack", 0.002f).set("ADSR2", "decay", 0.35f).set("ADSR2", "sustain", 0.0f).set("ADSR2", "release", 0.2f);
  b.set("DRIVE1", "drive", 0.35f).set("DRIVE1", "mix", 0.6f).set("DRIVE1", "level", 0.9f);
  b.wire("OSC1", "OUT", "MIX1", "IN1").wire("OSC2", "OUT", "MIX1", "IN2");
  b.wire("MIX1", "OUT", "FILTER1", "IN").wire("ADSR2", "OUT", "FILTER1", "CUTOFF", 0.45f);
  b.wire("FILTER1", "OUT", "AMP1", "IN").wire("ADSR1", "OUT", "AMP1", "GAIN");
  b.wire("AMP1", "OUT", "DRIVE1", "IN").wire("DRIVE1", "OUT", "MASTER", "IN");
  b.voices(4, VoiceMode::Mono, 0.04f, true);
  return b.p;
}

Patch glassPatch() {
  B b("GLASS", "Bell-like: inharmonic FM pair, long decay, delay and reverb");
  b.add("osc"); b.add("osc"); b.add("adsr"); b.add("amp"); b.add("delay"); b.add("reverb");
  b.set("OSC1", "wave", SINE).set("OSC1", "oct", 1);
  b.set("OSC2", "wave", SINE).set("OSC2", "oct", 1).set("OSC2", "semi", 7).set("OSC2", "fine", 14.0f).set("OSC2", "level", 0.6f);
  b.set("ADSR1", "attack", 0.002f).set("ADSR1", "decay", 2.8f).set("ADSR1", "sustain", 0.0f).set("ADSR1", "release", 1.6f);
  b.set("DELAY1", "time", 380.0f).set("DELAY1", "feedback", 0.4f).set("DELAY1", "mix", 0.28f);
  b.set("REVERB1", "size", 0.8f).set("REVERB1", "mix", 0.4f);
  b.wire("OSC2", "OUT", "OSC1", "FM", 0.12f);
  b.wire("OSC1", "OUT", "AMP1", "IN").wire("ADSR1", "OUT", "AMP1", "GAIN");
  b.wire("AMP1", "OUT", "DELAY1", "IN").wire("DELAY1", "OUT", "REVERB1", "IN");
  b.wire("REVERB1", "L", "MASTER", "L").wire("REVERB1", "R", "MASTER", "R");
  b.voices(8);
  return b.p;
}

Patch dronePatch() {
  B b("DRONE", "Slow evolving ambient pad: detuned saws, two LFOs, long tails");
  b.add("osc"); b.add("osc"); b.add("osc"); b.add("mixer"); b.add("filter"); b.add("lfo"); b.add("lfo");
  b.add("adsr"); b.add("amp"); b.add("delay"); b.add("reverb");
  b.set("OSC1", "wave", SAW).set("OSC1", "fine", -9.0f).set("OSC1", "level", 0.6f);
  b.set("OSC2", "wave", SAW).set("OSC2", "fine", 8.0f).set("OSC2", "level", 0.6f);
  b.set("OSC3", "wave", PULSE).set("OSC3", "oct", -1).set("OSC3", "level", 0.5f);
  b.set("MIX1", "level1", 0.55f).set("MIX1", "level2", 0.55f).set("MIX1", "level3", 0.5f);
  b.set("FILTER1", "cutoff", 900.0f).set("FILTER1", "res", 0.3f);
  b.set("LFO1", "rate", 0.11f).set("LFO1", "reset", 0);
  b.set("LFO2", "rate", 0.23f).set("LFO2", "reset", 0);
  b.set("ADSR1", "attack", 3.5f).set("ADSR1", "decay", 2.0f).set("ADSR1", "sustain", 0.9f).set("ADSR1", "release", 6.0f);
  b.set("DELAY1", "time", 620.0f).set("DELAY1", "feedback", 0.55f).set("DELAY1", "damp", 2500.0f).set("DELAY1", "mix", 0.35f);
  b.set("REVERB1", "size", 0.95f).set("REVERB1", "damp", 0.6f).set("REVERB1", "mix", 0.5f);
  b.wire("OSC1", "OUT", "MIX1", "IN1").wire("OSC2", "OUT", "MIX1", "IN2").wire("OSC3", "OUT", "MIX1", "IN3");
  b.wire("MIX1", "OUT", "FILTER1", "IN").wire("LFO1", "OUT", "FILTER1", "CUTOFF", 0.35f);
  b.wire("LFO2", "OUT", "OSC3", "PWM", 0.8f);
  b.wire("FILTER1", "OUT", "AMP1", "IN").wire("ADSR1", "OUT", "AMP1", "GAIN");
  b.wire("AMP1", "OUT", "DELAY1", "IN").wire("DELAY1", "OUT", "REVERB1", "IN");
  b.wire("REVERB1", "L", "MASTER", "L").wire("REVERB1", "R", "MASTER", "R");
  b.voices(6);
  return b.p;
}

Patch noisePatch() {
  B b("NOISE", "Filtered noise: wind and percussive hits, LFO on the band-pass");
  b.add("osc"); b.add("filter"); b.add("adsr"); b.add("adsr"); b.add("lfo"); b.add("amp"); b.add("reverb");
  b.set("OSC1", "wave", NOISE).set("OSC1", "level", 0.9f);
  b.set("FILTER1", "type", 2).set("FILTER1", "cutoff", 1200.0f).set("FILTER1", "res", 0.7f).set("FILTER1", "keytrack", 1.0f);
  b.set("ADSR1", "attack", 0.04f).set("ADSR1", "decay", 0.5f).set("ADSR1", "sustain", 0.5f).set("ADSR1", "release", 0.6f);
  b.set("ADSR2", "attack", 0.02f).set("ADSR2", "decay", 0.6f).set("ADSR2", "sustain", 0.2f).set("ADSR2", "release", 0.5f);
  b.set("LFO1", "wave", SINE).set("LFO1", "rate", 0.4f).set("LFO1", "reset", 0);
  b.set("REVERB1", "size", 0.7f).set("REVERB1", "mix", 0.3f);
  b.wire("OSC1", "OUT", "FILTER1", "IN").wire("ADSR2", "OUT", "FILTER1", "CUTOFF", 0.5f).wire("LFO1", "OUT", "FILTER1", "CUTOFF", 0.25f);
  b.wire("FILTER1", "OUT", "AMP1", "IN").wire("ADSR1", "OUT", "AMP1", "GAIN");
  b.wire("AMP1", "OUT", "REVERB1", "IN");
  b.wire("REVERB1", "L", "MASTER", "L").wire("REVERB1", "R", "MASTER", "R");
  b.voices(6);
  return b.p;
}

Patch leadPatch() {
  B b("LEAD", "Expressive mono lead: glide, vibrato, resonant filter, delay");
  b.add("osc"); b.add("osc"); b.add("mixer"); b.add("filter"); b.add("lfo"); b.add("adsr"); b.add("adsr"); b.add("amp"); b.add("delay");
  b.set("OSC1", "wave", SAW).set("OSC1", "fine", -6.0f);
  b.set("OSC2", "wave", PULSE).set("OSC2", "pw", 0.35f).set("OSC2", "fine", 7.0f).set("OSC2", "level", 0.6f);
  b.set("MIX1", "level1", 0.6f).set("MIX1", "level2", 0.5f);
  b.set("FILTER1", "cutoff", 1500.0f).set("FILTER1", "res", 0.5f).set("FILTER1", "drive", 0.2f).set("FILTER1", "keytrack", 0.5f);
  b.set("LFO1", "rate", 5.5f).set("LFO1", "depth", 1.0f).set("LFO1", "reset", 1);
  b.set("ADSR1", "attack", 0.02f).set("ADSR1", "decay", 0.3f).set("ADSR1", "sustain", 0.8f).set("ADSR1", "release", 0.3f);
  b.set("ADSR2", "attack", 0.01f).set("ADSR2", "decay", 0.4f).set("ADSR2", "sustain", 0.3f).set("ADSR2", "release", 0.3f);
  b.set("DELAY1", "time", 310.0f).set("DELAY1", "feedback", 0.35f).set("DELAY1", "mix", 0.25f);
  b.wire("LFO1", "OUT", "OSC1", "PITCH", 0.03f).wire("LFO1", "OUT", "OSC2", "PITCH", 0.03f);
  b.wire("OSC1", "OUT", "MIX1", "IN1").wire("OSC2", "OUT", "MIX1", "IN2");
  b.wire("MIX1", "OUT", "FILTER1", "IN").wire("ADSR2", "OUT", "FILTER1", "CUTOFF", 0.4f);
  b.wire("FILTER1", "OUT", "AMP1", "IN").wire("ADSR1", "OUT", "AMP1", "GAIN");
  b.wire("AMP1", "OUT", "DELAY1", "IN").wire("DELAY1", "OUT", "MASTER", "IN");
  b.voices(1, VoiceMode::Mono, 0.06f, true);
  return b.p;
}

Patch hooverPatch() {
  B b("HOOVER", "Rave hoover: detuned saw/pulse stack, pitch dive, PWM, resonant filter, drive");
  b.add("osc"); b.add("osc"); b.add("osc"); b.add("mixer"); b.add("filter"); b.add("lfo");
  b.add("adsr"); b.add("adsr"); b.add("amp"); b.add("drive"); b.add("delay"); b.add("reverb");
  b.set("OSC1", "wave", SAW).set("OSC1", "fine", -22.0f).set("OSC1", "level", 0.8f);
  b.set("OSC2", "wave", SAW).set("OSC2", "fine", 24.0f).set("OSC2", "level", 0.8f);
  b.set("OSC3", "wave", PULSE).set("OSC3", "oct", -1).set("OSC3", "pw", 0.5f).set("OSC3", "level", 0.8f);
  b.set("MIX1", "level1", 0.6f).set("MIX1", "level2", 0.6f).set("MIX1", "level3", 0.6f);
  b.set("FILTER1", "cutoff", 1800.0f).set("FILTER1", "res", 0.55f).set("FILTER1", "drive", 0.4f).set("FILTER1", "keytrack", 0.6f);
  b.set("LFO1", "wave", TRI).set("LFO1", "rate", 0.9f).set("LFO1", "depth", 1.0f).set("LFO1", "reset", 0);
  b.set("ADSR1", "attack", 0.01f).set("ADSR1", "decay", 0.4f).set("ADSR1", "sustain", 0.85f).set("ADSR1", "release", 0.35f);
  b.set("ADSR2", "attack", 0.0f).set("ADSR2", "decay", 0.35f).set("ADSR2", "sustain", 0.0f).set("ADSR2", "release", 0.2f);
  b.set("DRIVE1", "drive", 0.55f).set("DRIVE1", "mix", 0.7f).set("DRIVE1", "level", 0.8f);
  b.set("DELAY1", "time", 280.0f).set("DELAY1", "feedback", 0.3f).set("DELAY1", "mix", 0.18f);
  b.set("REVERB1", "size", 0.6f).set("REVERB1", "mix", 0.2f);
  // the signature "swoop": a fast envelope pulls all three oscillators down onto pitch
  b.wire("ADSR2", "OUT", "OSC1", "PITCH", 0.45f).wire("ADSR2", "OUT", "OSC2", "PITCH", 0.45f).wire("ADSR2", "OUT", "OSC3", "PITCH", 0.45f);
  b.wire("LFO1", "OUT", "OSC3", "PWM", 0.85f);
  b.wire("OSC1", "OUT", "MIX1", "IN1").wire("OSC2", "OUT", "MIX1", "IN2").wire("OSC3", "OUT", "MIX1", "IN3");
  b.wire("MIX1", "OUT", "FILTER1", "IN").wire("ADSR2", "OUT", "FILTER1", "CUTOFF", 0.3f);
  b.wire("FILTER1", "OUT", "AMP1", "IN").wire("ADSR1", "OUT", "AMP1", "GAIN");
  b.wire("AMP1", "OUT", "DRIVE1", "IN").wire("DRIVE1", "OUT", "DELAY1", "IN").wire("DELAY1", "OUT", "REVERB1", "IN");
  b.wire("REVERB1", "L", "MASTER", "L").wire("REVERB1", "R", "MASTER", "R");
  b.voices(6);
  return b.p;
}

// Alpha Juno style hoover: saw + two PWM'd pulses + sub saw, fast pitch-envelope dive,
// slow PWM, delayed vibrato, resonant low-pass, light saturation, stereo chorus.
Patch euroHooverPatch() {
  B b("EURO HOOVER", "90s rave hoover: saw/PWM stack, pitch dive, delayed vibrato, chorus. Great at C1-C3");
  b.add("osc"); b.add("osc"); b.add("osc"); b.add("osc"); b.add("mixer"); b.add("filter");
  b.add("lfo"); b.add("lfo"); b.add("adsr"); b.add("adsr"); b.add("amp"); b.add("drive"); b.add("chorus");
  b.set("OSC1", "wave", SAW).set("OSC1", "level", 0.9f);
  b.set("OSC2", "wave", PULSE).set("OSC2", "fine", -13.0f).set("OSC2", "pw", 0.5f).set("OSC2", "level", 0.8f);
  b.set("OSC3", "wave", PULSE).set("OSC3", "fine", 13.0f).set("OSC3", "pw", 0.5f).set("OSC3", "level", 0.8f);
  b.set("OSC4", "wave", SAW).set("OSC4", "oct", -1).set("OSC4", "fine", 4.0f).set("OSC4", "level", 0.8f);
  b.set("MIX1", "level1", 0.8f).set("MIX1", "level2", 0.6f).set("MIX1", "level3", 0.6f).set("MIX1", "level4", 0.65f);
  b.set("FILTER1", "cutoff", 3200.0f).set("FILTER1", "res", 0.25f).set("FILTER1", "drive", 0.3f).set("FILTER1", "keytrack", 0.5f);
  b.set("LFO1", "wave", SINE).set("LFO1", "rate", 0.6f).set("LFO1", "depth", 1.0f).set("LFO1", "reset", 0);
  b.set("LFO2", "wave", SINE).set("LFO2", "rate", 5.5f).set("LFO2", "depth", 1.0f).set("LFO2", "reset", 1)
      .set("LFO2", "delay", 0.35f).set("LFO2", "fade", 0.4f);
  b.set("ADSR1", "attack", 0.003f).set("ADSR1", "decay", 0.4f).set("ADSR1", "sustain", 0.85f).set("ADSR1", "release", 0.18f);
  b.set("ADSR2", "attack", 0.001f).set("ADSR2", "decay", 0.18f).set("ADSR2", "sustain", 0.0f).set("ADSR2", "release", 0.1f)
      .set("ADSR2", "velocity", 0.0f);
  b.set("DRIVE1", "drive", 0.3f).set("DRIVE1", "tone", 7000.0f).set("DRIVE1", "mix", 0.5f).set("DRIVE1", "level", 0.8f);
  b.set("CHORUS1", "rate", 0.7f).set("CHORUS1", "depth", 4.0f).set("CHORUS1", "mix", 0.55f).set("CHORUS1", "width", 1.0f);
  const char* oscs[4] = {"OSC1", "OSC2", "OSC3", "OSC4"};
  for (const char* o : oscs) {
    b.wire("ADSR2", "OUT", o, "PITCH", 0.6f);   // pitch envelope: starts +7.2 semitones, dives onto the note
    b.wire("LFO2", "OUT", o, "PITCH", 0.006f);  // vibrato, about +-7 cents, fades in after the attack
  }
  b.wire("LFO1", "OUT", "OSC2", "PWM", 0.5f).wire("LFO1", "OUT", "OSC3", "PWM", -0.5f);
  b.wire("OSC1", "OUT", "MIX1", "IN1").wire("OSC2", "OUT", "MIX1", "IN2")
      .wire("OSC3", "OUT", "MIX1", "IN3").wire("OSC4", "OUT", "MIX1", "IN4");
  b.wire("MIX1", "OUT", "FILTER1", "IN").wire("ADSR2", "OUT", "FILTER1", "CUTOFF", 0.2f);
  b.wire("FILTER1", "OUT", "AMP1", "IN").wire("ADSR1", "OUT", "AMP1", "GAIN");
  b.wire("AMP1", "OUT", "DRIVE1", "IN").wire("DRIVE1", "OUT", "CHORUS1", "IN");
  b.wire("CHORUS1", "L", "MASTER", "L").wire("CHORUS1", "R", "MASTER", "R");
  b.voices(6);
  return b.p;
}

}  // namespace

const std::vector<PresetInfo>& factoryPresets() {
  static const std::vector<PresetInfo> list = {
      {"init", "INIT", initPatch},   {"sub", "SUB", subPatch},     {"glass", "GLASS", glassPatch},
      {"drone", "DRONE", dronePatch}, {"noise", "NOISE", noisePatch}, {"lead", "LEAD", leadPatch},
      {"hoover", "HOOVER", hooverPatch}, {"euro_hoover", "EURO HOOVER", euroHooverPatch},
  };
  return list;
}

}  // namespace nocturne
