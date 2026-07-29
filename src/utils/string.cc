/*
  This file is part of Leela Chess Zero.
  Copyright (C) 2018 The LCZero Authors

  Leela Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Leela Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Leela Chess.  If not, see <http://www.gnu.org/licenses/>.

  Additional permission under GNU GPL version 3 section 7

  If you modify this Program, or any covered work, by linking or
  combining it with NVIDIA Corporation's libraries from the NVIDIA CUDA
  Toolkit and the NVIDIA CUDA Deep Neural Network library (or a
  modified version of those libraries), containing parts covered by the
  terms of the respective license agreement, the licensors of this
  Program grant you additional permission to convey the resulting work.
*/

#include "utils/string.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace lczero {

std::string StrJoin(const std::vector<std::string>& strings,
                    std::string_view delim) {
  std::string res;
  for (const auto& str : strings) {
    if (!res.empty()) res += delim;
    res += str;
  }
  return res;
}

std::vector<std::string> StrSplitAtWhitespace(std::string_view str) {
  std::vector<std::string> result;
  auto begin = str.begin();
  const auto end = str.end();
  while (begin != end) {
    begin =
        std::find_if_not(begin, end, [](int ch) { return std::isspace(ch); });
    if (begin == end) break;
    auto word_end =
        std::find_if(begin, end, [](int ch) { return std::isspace(ch); });
    result.emplace_back(begin, word_end);
    begin = word_end;
  }
  return result;
}

std::vector<std::string> StrSplit(std::string_view str,
                                  std::string_view delim) {
  std::vector<std::string> result;
  for (std::string_view::size_type pos = 0, next = 0;
       pos != std::string_view::npos; pos = next) {
    next = str.find(delim, pos);
    result.emplace_back(str.substr(pos, next - pos));
    if (next != std::string_view::npos) next += delim.size();
  }
  return result;
}

std::vector<int> ParseIntList(std::string_view str) {
  std::vector<int> result;
  for (const auto& x : StrSplit(str, ",")) {
    result.push_back(std::stoi(x));
  }
  return result;
}

std::string LeftTrim(std::string_view str) {
  const auto it = std::find_if(str.begin(), str.end(),
                               [](int ch) { return !std::isspace(ch); });
  str.remove_prefix(std::distance(str.begin(), it));
  return std::string(str);
}

std::string RightTrim(std::string_view str) {
  auto it = std::find_if(str.rbegin(), str.rend(),
                         [](int ch) { return !std::isspace(ch); });
  str.remove_suffix(std::distance(str.rbegin(), it));
  return std::string(str);
}

std::string Trim(std::string_view str) {
  const auto left_it = std::find_if(str.begin(), str.end(),
                                    [](int ch) { return !std::isspace(ch); });
  str.remove_prefix(std::distance(str.begin(), left_it));
  auto right_it = std::find_if(str.rbegin(), str.rend(),
                               [](int ch) { return !std::isspace(ch); });
  str.remove_suffix(std::distance(str.rbegin(), right_it));
  return std::string(str);
}

bool StringsEqualIgnoreCase(std::string_view a, std::string_view b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char a, char b) {
    return std::tolower(a) == std::tolower(b);
  });
}

std::vector<std::string> FlowText(std::string_view src, size_t width) {
  std::vector<std::string> result;
  auto paragraphs = StrSplit(src, "\n");
  for (const auto& paragraph : paragraphs) {
    result.emplace_back();
    auto words = StrSplit(paragraph, " ");
    for (const auto& word : words) {
      if (result.back().empty()) {
        // First word in line, always add.
      } else if (result.back().size() + word.size() + 1 > width) {
        // The line doesn't have space for a new word.
        result.emplace_back();
      } else {
        // Appending to the current line.
        result.back() += " ";
      }
      result.back() += word;
    }
  }
  return result;
}

}  // namespace lczero
