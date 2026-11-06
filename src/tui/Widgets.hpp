#pragma once
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "modules/ModuleSpec.hpp"

namespace nocturne::tui {

namespace theme {
inline const ftxui::Color text = ftxui::Color::RGB(208, 208, 218);
inline const ftxui::Color dim = ftxui::Color::RGB(112, 112, 126);
inline const ftxui::Color border = ftxui::Color::RGB(72, 72, 88);
inline const ftxui::Color bg = ftxui::Color::RGB(16, 16, 22);
inline const ftxui::Color panel = ftxui::Color::RGB(26, 26, 34);
inline const ftxui::Color purple = ftxui::Color::RGB(176, 128, 255);
inline const ftxui::Color amber = ftxui::Color::RGB(245, 176, 65);
inline const ftxui::Color cyan = ftxui::Color::RGB(88, 208, 228);
inline const ftxui::Color red = ftxui::Color::RGB(240, 90, 90);
inline const ftxui::Color green = ftxui::Color::RGB(120, 220, 140);
}  // namespace theme

// Compact bar for module cards, e.g. "█████░░░".
ftxui::Element paramBar(const ParamSpec& p, float value, int width);
// Full-width control: "◀──────●────────▶".
ftxui::Element paramSlider(const ParamSpec& p, float value, int width, ftxui::Color accent);
ftxui::Element levelMeter(float level, int width);  // linear 0..1, shown on a dB scale
std::string dbString(float linear);
std::string fixedText(const std::string& s, int width);  // pad / truncate to a column width
std::string repeatStr(const char* s, int n);

}  // namespace nocturne::tui
