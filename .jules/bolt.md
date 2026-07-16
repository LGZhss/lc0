## 2024-07-16 - [String Splitting Performance Bottleneck]
**Learning:** Standard library `std::istringstream` and `std::string::find` combined with substring allocations in `StrSplit` and `StrSplitAtWhitespace` create a measurable performance overhead compared to using `std::string_view`.
**Action:** Use `std::string_view` to locate delimiters to avoid intermediate string allocations, even if returning `std::vector<std::string>`.
