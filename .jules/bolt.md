## 2024-05-19 - [O(N) to O(log N) optimizations in hot loops]
**Learning:** O(N) `std::find` in hot search loops (like evaluating edges for `root_move_filter_`) can be optimized safely by applying a pre-sort to `MoveList` and using `std::binary_search`. This also applies when transforming move strings in initialization functions.
**Action:** Always check `std::find` calls within search loops on moderately sized lists. If the list is static after creation, sort it and use `std::binary_search` instead, implementing an `operator<` if necessary for custom wrapper types like `Move`.
