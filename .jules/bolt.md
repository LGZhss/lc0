## 2024-07-04 - [std::erase_if]
**Learning:** For C++20 codebases like Leela Chess Zero, using `std::erase_if` is a standard, clean, and potentially faster replacement for the `erase-remove` idiom, especially on vectors/lists.
**Action:** Identify and replace instances of `erase-remove` with `std::erase_if` for better performance and readability.
