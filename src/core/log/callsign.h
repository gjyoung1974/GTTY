// Callsign recognition in received text and Super Check Partial support.
#pragma once
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gtty {

// Heuristic: prefix (1-3 alnum incl. a letter), one digit, 1-4 letter suffix,
// optional /P-style suffix or prefix.
bool looksLikeCallsign(const std::string& word);

// Returns [start, length) spans of words in `text` that look like callsigns.
std::vector<std::pair<size_t, size_t>> findCallsigns(const std::string& text);

class ScpDatabase {
 public:
  bool load(const std::string& path, std::string& err);  // MASTER.SCP format
  bool loaded() const { return !calls_.empty(); }
  size_t size() const { return calls_.size(); }
  bool contains(const std::string& call) const { return calls_.count(call) > 0; }
  // Calls containing `fragment` (at least 2 chars), up to `max`.
  std::vector<std::string> partials(const std::string& fragment, size_t max = 50) const;

 private:
  std::unordered_set<std::string> calls_;
  std::vector<std::string> sorted_;
};

}  // namespace gtty
