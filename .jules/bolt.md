<<<<<<< HEAD
## 2025-07-02 - Optimize ReverseBytesInBytes
**Learning:** `ReverseBytesInBytes` was doing a manual 64-bit byte swap in 14 instructions. This operation is effectively a `bswap` which most architectures have a dedicated instruction for. This is called heavily during neural network evaluation and data encoding.
**Action:** Use compiler intrinsic `__builtin_bswap64` or MSVC equivalent `_byteswap_uint64` instead of manual shifts and masks, turning 14 ops into 1 opcode.
=======

## 2024-07-03 - Pre-allocate vector in search tree root selection
**Learning:** In high-frequency hot paths like the root child selection during search phase (`GetBestRootChildWithTemperature`), the vector allocations such as `cumulative_sums` are dynamically expanding `std::vector`. Since we know the upper bound of edges (`root_node_->GetNumEdges()`), we can pre-allocate it to avoid memory allocations and copying over elements.
**Action:** Always pre-allocate `std::vector` variables with `reserve` in inner loops / heavy methods if the capacity is known upfront to prevent dynamic resizing, especially in tree traversal and search logic.
>>>>>>> origin/pr/2

## 2025-07-04 - Pre-allocate vector when traversing linked-list structures
**Learning:** When collecting elements along a tree or linked-list path to the root (like retrieving `Move` paths using `GetParent()`), appending directly into a `std::vector` causes multiple dynamic memory reallocations. Since the path length isn't known explicitly but can be found by counting, it represents a hidden performance overhead on hot paths.
**Action:** Use a two-pass approach for such collections: first traverse the structure to count the required size (depth), call `.reserve()` on the vector, and then perform a second pass to populate it.
