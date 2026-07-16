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
#include <string_view>
#include <vector>

namespace lczero {

std::string StrJoin(const std::vector<std::string>& strings,
                    const std::string& delim) {
  std::string res;
  for (const auto& str : strings) {
    if (!res.empty()) res += delim;
    res += str;
  }
  return res;
}

std::vector<std::string> StrSplitAtWhitespace(const std::string& str) {
  std::vector<std::string> result;
  std::string_view sv = str;
  size_t start = sv.find_first_not_of(" \t\n\r\f\v");
  while (start != std::string_view::npos) {
    size_t end = sv.find_first_of(" \t\n\r\f\v", start);
    if (end == std::string_view::npos) {
        result.emplace_back(sv.substr(start));
        break;
    }
    result.emplace_back(sv.substr(start, end - start));
    start = sv.find_first_not_of(" \t\n\r\f\v", end);
  }
  return result;
}

std::vector<std::string> StrSplit(const std::string& str,
                                  const std::string& delim) {
  std::vector<std::string> result;
  std::string_view sv = str;
  std::string_view delim_sv = delim;

  auto pos = sv.find(delim_sv);
  while (pos != std::string_view::npos) {
    result.emplace_back(sv.substr(0, pos));
    sv.remove_prefix(pos + delim_sv.size());
    pos = sv.find(delim_sv);
  }
  result.emplace_back(sv);
  return result;
}

std::vector<int> ParseIntList(const std::string& str) {
  std::vector<int> result;
  for (const auto& x : StrSplit(str, ",")) {
    result.push_back(std::stoi(x));
  }
  return result;
}

std::string LeftTrim(std::string str) {
  const auto it = std::find_if(str.begin(), str.end(),
                         [](int ch) { return !std::isspace(ch); });
  str.erase(str.begin(), it);
  return str;
}

std::string RightTrim(std::string str) {
  auto it = std::find_if(str.rbegin(), str.rend(),
                         [](int ch) { return !std::isspace(ch); });
  str.erase(it.base(), str.end());
  return str;
}

std::string Trim(std::string str) {
  return LeftTrim(RightTrim(std::move(str)));
}

bool StringsEqualIgnoreCase(const std::string& a, const std::string& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char a, char b) {
    return std::tolower(a) == std::tolower(b);
  });
}

std::vector<std::string> FlowText(const std::string& src, size_t width) {
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
