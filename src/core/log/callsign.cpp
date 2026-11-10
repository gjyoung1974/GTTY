#include "core/log/callsign.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace gtty {

namespace {
bool isCallCore(const std::string& s) {
  // Find the last digit in the first part; need letters after it.
  if (s.size() < 3 || s.size() > 7) return false;
  size_t digit = std::string::npos;
  for (size_t i = 0; i < s.size(); ++i)
    if (std::isdigit(static_cast<unsigned char>(s[i]))) digit = i;
  if (digit == std::string::npos || digit == 0 && !std::isdigit(static_cast<unsigned char>(s[0]))) return false;
  // Prefix: 1-3 chars with at least one letter (or a digit-first prefix like 4X, 9A)
  if (digit > 3) {
    // allow e.g. 'A71AA' style (prefix 2 chars + digit at 2) - digit index <= 3 generally
    return false;
  }
  bool prefixHasLetter = false;
  for (size_t i = 0; i < digit; ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (!std::isalnum(c)) return false;
    if (std::isalpha(c)) prefixHasLetter = true;
  }
  if (digit > 0 && !prefixHasLetter) return false;
  if (digit == 0) return false;
  size_t suffix = s.size() - digit - 1;
  if (suffix < 1 || suffix > 4) return false;
  for (size_t i = digit + 1; i < s.size(); ++i)
    if (!std::isalpha(static_cast<unsigned char>(s[i]))) return false;
  return true;
}
}  // namespace

bool looksLikeCallsign(const std::string& wordIn) {
  std::string w;
  for (char c : wordIn) w.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  if (w.empty()) return false;
  // Split on '/', one part must be a callsign core, others short.
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i <= w.size(); ++i) {
    if (i == w.size() || w[i] == '/') {
      parts.push_back(w.substr(start, i - start));
      start = i + 1;
    }
  }
  if (parts.size() > 3) return false;
  bool core = false;
  for (const auto& p : parts) {
    if (p.empty() || p.size() > 7) return false;
    if (isCallCore(p)) core = true;
    else if (p.size() > 4) return false;
    for (char c : p) if (!std::isalnum(static_cast<unsigned char>(c))) return false;
  }
  return core;
}

std::vector<std::pair<size_t, size_t>> findCallsigns(const std::string& text) {
  std::vector<std::pair<size_t, size_t>> out;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && !(std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '/')) ++i;
    size_t s = i;
    while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '/')) ++i;
    if (i > s && looksLikeCallsign(text.substr(s, i - s))) out.emplace_back(s, i - s);
  }
  return out;
}

bool ScpDatabase::load(const std::string& path, std::string& err) {
  std::ifstream f(path);
  if (!f) { err = "cannot open " + path; return false; }
  calls_.clear();
  sorted_.clear();
  std::string line;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    for (auto& c : line) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (calls_.insert(line).second) sorted_.push_back(line);
  }
  std::sort(sorted_.begin(), sorted_.end());
  return true;
}

std::vector<std::string> ScpDatabase::partials(const std::string& fragIn, size_t max) const {
  std::vector<std::string> out;
  if (fragIn.size() < 2) return out;
  std::string frag;
  for (char c : fragIn) frag.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  for (const auto& c : sorted_) {
    if (c.find(frag) != std::string::npos) {
      out.push_back(c);
      if (out.size() >= max) break;
    }
  }
  return out;
}

}  // namespace gtty
