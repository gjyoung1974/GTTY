#pragma once
#include <string>
#include <vector>

namespace gtty {
// Maidenhead locator (4 or 6 characters, e.g. FN31 / FN31pr).
bool looksLikeGrid(const std::string& word);
// First grid locator found in `text`, or "".
std::string findGrid(const std::string& text);
}  // namespace gtty
