#include "core/baudot.h"

#include <cctype>

namespace gtty {

namespace {
// Index = 5-bit code. Control codes are represented with:
//   '\0' blank, '\n' LF, '\r' CR, '\x1b' FIGS, '\x1f' LTRS, '\a' BELL, '\x05' ENQ(WRU)
const char kLetters[33] = {
    '\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
    'T',  'Z', 'L',  'W', 'H', 'Y', 'P', 'Q', 'O',  'B', 'G', '\x1b', 'M', 'X', 'V', '\x1f', 0};

const char kFiguresITA2[33] = {
    '\0', '3', '\n', '-', ' ', '\'', '8', '7', '\r', '\x05', '4', '\a', ',', '!', ':', '(',
    '5',  '+', ')',  '2', '#', '6',  '0', '1', '9',  '?',    '&', '\x1b', '.', '/', '=', '\x1f', 0};

const char kFiguresUSTTY[33] = {
    '\0', '3', '\n', '-', ' ', '\a', '8', '7', '\r', '$', '4', '\'', ',', '!', ':', '(',
    '5',  '"', ')',  '2', '#', '6',  '0', '1', '9',  '?', '&', '\x1b', '.', '/', ';', '\x1f', 0};
}  // namespace

namespace baudot {
const char* lettersTable() { return kLetters; }
const char* figuresTable(BaudotVariant v) {
  return v == BaudotVariant::ITA2 ? kFiguresITA2 : kFiguresUSTTY;
}
}  // namespace baudot

std::vector<uint8_t> BaudotEncoder::encode(char c) {
  std::vector<uint8_t> out;
  if (c == '\n') {
    out.push_back(baudot::CR);
    out.push_back(baudot::LF);
    return out;
  }
  if (c == '\r') return out;  // '\n' already produces CR LF
  c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

  // Shift-agnostic codes first.
  if (c == ' ') {
    out.push_back(baudot::SPACE);
    if (uos_ && shift_ == Shift::Figures) shift_ = Shift::Letters;
    return out;
  }

  const char* figs = baudot::figuresTable(variant_);
  for (int i = 0; i < 32; ++i) {
    if (kLetters[i] == c && i != 0 && i != 0x1b && i != 0x1f) {
      if (shift_ != Shift::Letters) { out.push_back(baudot::LTRS); shift_ = Shift::Letters; }
      out.push_back(static_cast<uint8_t>(i));
      return out;
    }
  }
  for (int i = 0; i < 32; ++i) {
    if (figs[i] == c && i != 0 && i != 0x1b && i != 0x1f && i != 2 && i != 4 && i != 8) {
      if (shift_ != Shift::Figures) { out.push_back(baudot::FIGS); shift_ = Shift::Figures; }
      out.push_back(static_cast<uint8_t>(i));
      return out;
    }
  }
  return out;  // unsupported character
}

std::vector<uint8_t> BaudotEncoder::encode(const std::string& s) {
  std::vector<uint8_t> out;
  for (char c : s) {
    auto codes = encode(c);
    out.insert(out.end(), codes.begin(), codes.end());
  }
  return out;
}

std::optional<char> BaudotDecoder::decode(uint8_t code) {
  code &= 0x1f;
  switch (code) {
    case baudot::LTRS: figures_ = false; return std::nullopt;
    case baudot::FIGS: figures_ = true; return std::nullopt;
    case baudot::BLANK: return std::nullopt;
    case baudot::SPACE:
      if (uos_) figures_ = false;
      return ' ';
    default: break;
  }
  const char* table = figures_ ? baudot::figuresTable(variant_) : kLetters;
  char c = table[code];
  if (c == '\0' || c == '\x05') return std::nullopt;
  return c;
}

}  // namespace gtty
