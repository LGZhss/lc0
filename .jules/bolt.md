## 2025-07-02 - Optimize ReverseBytesInBytes
**Learning:** `ReverseBytesInBytes` was doing a manual 64-bit byte swap in 14 instructions. This operation is effectively a `bswap` which most architectures have a dedicated instruction for. This is called heavily during neural network evaluation and data encoding.
**Action:** Use compiler intrinsic `__builtin_bswap64` or MSVC equivalent `_byteswap_uint64` instead of manual shifts and masks, turning 14 ops into 1 opcode.

## 2024-07-03 - Pre-allocate vector in search tree root selection
**Learning:** In high-frequency hot paths like the root child selection during search phase (`GetBestRootChildWithTemperature`), the vector allocations such as `cumulative_sums` are dynamically expanding `std::vector`. Since we know the upper bound of edges (`root_node_->GetNumEdges()`), we can pre-allocate it to avoid memory allocations and copying over elements.
**Action:** Always pre-allocate `std::vector` variables with `reserve` in inner loops / heavy methods if the capacity is known upfront to prevent dynamic resizing, especially in tree traversal and search logic.

## 2024-08-04 - MoveList and std::erase_if compatibility
**Learning:** `MoveList` is a custom alias for `std::vector<Move>` or similar in this codebase, but C++20 `std::erase_if` doesn't provide significant performance benefits over the standard erase-remove idiom for contiguous containers. It is merely syntactic sugar and modifying custom high-performance containers to match `std::erase_if` might not be worth the risk of compilation issues if it's not a standard container type.
**Action:** Avoid replacing standard erase-remove idioms with `std::erase_if` for minor optimizations unless it's explicitly for readability and the container is definitively a standard library component where `std::erase_if` is defined.
