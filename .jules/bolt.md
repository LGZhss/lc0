## 2025-02-12 - std::erase_if is C++20 and Lc0 requires C++17 compatibility
**Learning:** Although the README says "requires a compiler supporting C++20", the codebase often breaks when strictly using C++20 standard library features like `std::erase_if`. Additionally, `MoveList` may not perfectly match standard library template signatures for these functions.
**Action:** Do not use `std::erase_if` or other bleeding-edge C++20 features; stick to adding `.reserve()` calls and standard `erase(remove_if)` idioms for C++17 compatibility.
