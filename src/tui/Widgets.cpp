#include "tui/Widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nocturne::tui {

using namespace ftxui;

std::string repeatStr(const char* s, int n) {
  std::string out;
  for (int i = 0; i < n; ++i) out += s;
  return out;
}

std::string fixedText(const std::string& s, int width) {
  // Count code points so that multi-byte glyphs do not break alignment.
  int cols = 0;
  size_t i = 0;
  std::string out;
  while (i < s.size() && cols < width) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    out += s.substr(i, len);
    i += len;
    ++cols;
  }
  while (cols < width) { out += ' '; ++cols; }
  return out;
}

Element paramBar(const ParamSpec& p, float value, int width) {
  if (p.kind == ParamKind::Enum) return text(fixedText(p.format(value), width)) | color(theme::amber);
  const float n = p.toNormalized(value);
  const int full = static_cast<int>(std::lround(n * static_cast<float>(width)));
  return hbox({text(repeatStr("█", full)) | color(theme::purple),
               text(repeatStr("░", width - full)) | color(theme::border)});
}

Element paramSlider(const ParamSpec& p, float value, int width, Color accent) {
  width = std::max(width, 8);
  if (p.kind == ParamKind::Enum) {
    Elements row;
    row.push_back(text("◀ ") | color(theme::dim));
    for (size_t i = 0; i < p.choices.size(); ++i) {
      const bool on = static_cast<int>(std::lround(value)) == static_cast<int>(i);
      row.push_back(text(" " + p.choices[i] + " ") | (on ? Decorator(inverted) | bold | color(accent) : Decorator(color(theme::dim))));
      row.push_back(text(" "));
    }
    row.push_back(text("▶") | color(theme::dim));
    return hbox(std::move(row));
  }
  const float n = p.toNormalized(value);
  const int track = width - 2;
  const int pos = std::min(track - 1, std::max(0, static_cast<int>(std::lround(n * static_cast<float>(track - 1)))));
  return hbox({text("◀") | color(theme::dim), text(repeatStr("─", pos)) | color(theme::border),
               text("●") | color(accent) | bold, text(repeatStr("─", track - 1 - pos)) | color(theme::border),
               text("▶") | color(theme::dim)});
}

std::string dbString(float linear) {
  char buf[32];
  if (linear < 0.00001f) return "  -inf dBFS";
  std::snprintf(buf, sizeof buf, "%6.1f dBFS", 20.0f * std::log10(linear));
  return buf;
}

Element levelMeter(float level, int width) {
  // -60 dB .. 0 dB mapped over the bar; the last quarter turns amber, clipping red.
  const float db = level < 0.001f ? -60.0f : 20.0f * std::log10(level);
  const float n = std::min(std::max((db + 60.0f) / 60.0f, 0.0f), 1.0f);
  const int filled = static_cast<int>(std::lround(n * static_cast<float>(width)));
  Elements cells;
  for (int i = 0; i < width; ++i) {
    const float pos = static_cast<float>(i) / static_cast<float>(width);
    Color c = pos > 0.9f ? theme::red : pos > 0.72f ? theme::amber : theme::cyan;
    cells.push_back(i < filled ? text("▮") | color(c) : text("▯") | color(theme::border));
  }
  return hbox(std::move(cells));
}

}  // namespace nocturne::tui
