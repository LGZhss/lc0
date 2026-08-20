<<<<<<< HEAD
<<<<<<< HEAD
## 2025-07-02 - Optimize ReverseBytesInBytes
**Learning:** `ReverseBytesInBytes` was doing a manual 64-bit byte swap in 14 instructions. This operation is effectively a `bswap` which most architectures have a dedicated instruction for. This is called heavily during neural network evaluation and data encoding.
**Action:** Use compiler intrinsic `__builtin_bswap64` or MSVC equivalent `_byteswap_uint64` instead of manual shifts and masks, turning 14 ops into 1 opcode.
=======

## 2024-07-03 - Pre-allocate vector in search tree root selection
**Learning:** In high-frequency hot paths like the root child selection during search phase (`GetBestRootChildWithTemperature`), the vector allocations such as `cumulative_sums` are dynamically expanding `std::vector`. Since we know the upper bound of edges (`root_node_->GetNumEdges()`), we can pre-allocate it to avoid memory allocations and copying over elements.
**Action:** Always pre-allocate `std::vector` variables with `reserve` in inner loops / heavy methods if the capacity is known upfront to prevent dynamic resizing, especially in tree traversal and search logic.
>>>>>>> origin/pr/2
=======
## 2024-05-19 - [O(N) to O(log N) optimizations in hot loops]
**Learning:** O(N) `std::find` in hot search loops (like evaluating edges for `root_move_filter_`) can be optimized safely by applying a pre-sort to `MoveList` and using `std::binary_search`. This also applies when transforming move strings in initialization functions.
**Action:** Always check `std::find` calls within search loops on moderately sized lists. If the list is static after creation, sort it and use `std::binary_search` instead, implementing an `operator<` if necessary for custom wrapper types like `Move`.
>>>>>>> origin/pr/14
