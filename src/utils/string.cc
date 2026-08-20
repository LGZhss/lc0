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
  if (strings.empty()) return "";

  // Pre-calculate total length to avoid multiple allocations during concatenation
  size_t total_len = 0;
  for (const auto& str : strings) {
    total_len += str.size();
  }
  total_len += delim.size() * (strings.size() - 1);

  std::string res;
  res.reserve(total_len);

  res += strings[0];
  for (size_t i = 1; i < strings.size(); ++i) {
    res += delim;
    res += strings[i];
  }
  return res;
}

std::vector<std::string> StrSplitAtWhitespace(std::string_view str) {
  std::vector<std::string> result;
  size_t start = str.find_first_not_of(" \t\n\r\v\f");
  while (start != std::string_view::npos) {
    size_t end = str.find_first_of(" \t\n\r\v\f", start);
    if (end == std::string_view::npos) {
      result.emplace_back(str.substr(start));
      break;
    }
    result.emplace_back(str.substr(start, end - start));
    start = str.find_first_not_of(" \t\n\r\v\f", end + 1);
  }
  return result;
}

std::vector<std::string> StrSplit(std::string_view str,
                                  std::string_view delim) {
  std::vector<std::string> result;
  for (std::string_view::size_type pos = 0, next = 0; pos != std::string_view::npos;
       pos = next) {
    next = str.find(delim, pos);
    if (next == std::string_view::npos) {
      result.emplace_back(str.substr(pos));
      break;
    } else {
      result.emplace_back(str.substr(pos, next - pos));
      next += delim.size();
    }
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
                               [](unsigned char ch) { return !std::isspace(ch); });
  return std::string(str.substr(std::distance(str.begin(), it)));
}

std::string RightTrim(std::string_view str) {
  auto it = std::find_if(str.rbegin(), str.rend(),
                         [](unsigned char ch) { return !std::isspace(ch); });
  return std::string(str.substr(0, std::distance(it, str.rend())));
}

std::string Trim(std::string_view str) {
  const auto start = std::find_if(str.begin(), str.end(),
                                  [](unsigned char ch) { return !std::isspace(ch); });
  if (start == str.end()) return "";
  const auto end = std::find_if(str.rbegin(), str.rend(),
                                [](unsigned char ch) { return !std::isspace(ch); }).base();
  return std::string(start, end);
}

bool StringsEqualIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char ca, char cb) {
    return std::tolower(static_cast<unsigned char>(ca)) == std::tolower(static_cast<unsigned char>(cb));
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
