#include "core/log/grid.h"

#include <cctype>

namespace gtty {

bool looksLikeGrid(const std::string& w) {
  if (w.size() != 4 && w.size() != 6) return false;
  auto up = [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); };
  if (up(w[0]) < 'A' || up(w[0]) > 'R' || up(w[1]) < 'A' || up(w[1]) > 'R') return false;
  if (!std::isdigit(static_cast<unsigned char>(w[2])) || !std::isdigit(static_cast<unsigned char>(w[3]))) return false;
  if (w.size() == 6 && (up(w[4]) < 'A' || up(w[4]) > 'X' || up(w[5]) < 'A' || up(w[5]) > 'X')) return false;
  // "RR73" is a signal report token in digital modes, not a grid.
  if (up(w[0]) == 'R' && up(w[1]) == 'R' && w[2] == '7' && w[3] == '3') return false;
  return true;
}

std::string findGrid(const std::string& text) {
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && !std::isalnum(static_cast<unsigned char>(text[i]))) ++i;
    size_t s = i;
    while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]))) ++i;
    if (i > s) {
      std::string w = text.substr(s, i - s);
      if (looksLikeGrid(w)) {
        for (auto& c : w) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (w.size() == 6) { w[4] = static_cast<char>(std::tolower(w[4])); w[5] = static_cast<char>(std::tolower(w[5])); }
        return w;
      }
    }
  }
  return "";
}

}  // namespace gtty
