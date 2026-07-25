## 2024-05-24 - Preallocating Vectors in Hot Paths
**Learning:** We are in a highly-optimized C++ codebase (a chess engine) where memory allocations in hot paths can have an outsized impact on performance. Several instances in search.cc show vectors being dynamically expanded during tight loops (e.g., `cumulative_sums.push_back` and `scores.emplace_back`), but without calling `reserve()` prior.
**Action:** Always pre-allocate (`reserve`) vector capacity when the maximum size is known, especially in critical tree search functions.
