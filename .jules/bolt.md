## 2023-11-20 - [Pre-allocate memory for vector]
**Learning:** Reserving vector capacity with `reserve()` before populating it dynamically prevents memory reallocations in tight loops (e.g., `Search::GetBestChildrenNoTemperature`).
**Action:** Always check loop structures where a vector is populated and `push_back` is used if the maximum potential size of the vector is known in advance. Pre-allocate using `reserve()` to optimize memory management.
