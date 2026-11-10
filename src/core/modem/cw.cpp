#include "core/modem/cw.h"

#include <cctype>
#include <cmath>

namespace gtty {

namespace {
const char* morse(char c) {
  switch (std::toupper(static_cast<unsigned char>(c))) {
    case 'A': return ".-";    case 'B': return "-...";  case 'C': return "-.-.";  case 'D': return "-..";
    case 'E': return ".";     case 'F': return "..-.";  case 'G': return "--.";   case 'H': return "....";
    case 'I': return "..";    case 'J': return ".---";  case 'K': return "-.-";   case 'L': return ".-..";
    case 'M': return "--";    case 'N': return "-.";    case 'O': return "---";   case 'P': return ".--.";
    case 'Q': return "--.-";  case 'R': return ".-.";   case 'S': return "...";   case 'T': return "-";
    case 'U': return "..-";   case 'V': return "...-";  case 'W': return ".--";   case 'X': return "-..-";
    case 'Y': return "-.--";  case 'Z': return "--..";
    case '0': return "-----"; case '1': return ".----"; case '2': return "..---"; case '3': return "...--";
    case '4': return "....-"; case '5': return ".....";  case '6': return "-....";  case '7': return "--...";
    case '8': return "---.."; case '9': return "----.";
    case '/': return "-..-."; case '?': return "..--.."; case '.': return ".-.-.-"; case ',': return "--..--";
    case '=': return "-...-"; case '+': return ".-.-.";  case '-': return "-....-"; case '@': return ".--.-.";
    default: return nullptr;
  }
}
}  // namespace

std::vector<CwElement> morseElements(const std::string& text, double wpm) {
  std::vector<CwElement> out;
  if (wpm <= 0) wpm = 20;
  const double dit = 1.2 / wpm;
  bool firstChar = true;
  for (char c : text) {
    if (c == ' ') {
      if (!out.empty()) out.push_back({false, 7 * dit});   // word gap (replaces the 3-unit char gap)
      firstChar = true;
      continue;
    }
    const char* m = morse(c);
    if (!m) continue;
    if (!firstChar) out.push_back({false, 3 * dit});
    firstChar = false;
    for (const char* p = m; *p; ++p) {
      if (p != m) out.push_back({false, dit});
      out.push_back({true, *p == '-' ? 3 * dit : dit});
    }
  }
  // Collapse a char gap that directly follows a word gap.
  std::vector<CwElement> clean;
  for (const auto& e : out) {
    if (!clean.empty() && !e.on && !clean.back().on) clean.back().seconds = std::max(clean.back().seconds, e.seconds);
    else clean.push_back(e);
  }
  return clean;
}

double cwDuration(const std::vector<CwElement>& el) {
  double t = 0;
  for (const auto& e : el) t += e.seconds;
  return t;
}

void CwTone::prepare(const std::vector<CwElement>& el, double fs, double toneHz, float amplitude) {
  el_ = el;
  idx_ = 0;
  fs_ = fs > 0 ? fs : 48000;
  remain_ = el_.empty() ? 0 : el_[0].seconds * fs_;
  phase_ = 0;
  inc_ = 2 * M_PI * toneHz / fs_;
  amp_ = amplitude;
  env_ = 0;
  done_.store(el_.empty());
}

void CwTone::generate(float* out, size_t n) {
  const float ramp = static_cast<float>(1.0 / (0.005 * fs_));   // 5 ms edges
  for (size_t i = 0; i < n; ++i) {
    while (idx_ < el_.size() && remain_ <= 0) {
      ++idx_;
      if (idx_ < el_.size()) remain_ = el_[idx_].seconds * fs_;
    }
    bool on = idx_ < el_.size() && el_[idx_].on;
    if (idx_ >= el_.size()) {
      if (env_ <= 0.001f) { done_.store(true); }
    }
    float target = on ? 1.f : 0.f;
    env_ += (target - env_) > ramp ? ramp : (target - env_) < -ramp ? -ramp : (target - env_);
    out[i] = amp_ * env_ * static_cast<float>(std::sin(phase_));
    phase_ += inc_;
    if (phase_ > 2 * M_PI) phase_ -= 2 * M_PI;
    remain_ -= 1;
  }
}

}  // namespace gtty
