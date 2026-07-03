
## 2024-07-03 - Pre-allocate vector in search tree root selection
**Learning:** In high-frequency hot paths like the root child selection during search phase (`GetBestRootChildWithTemperature`), the vector allocations such as `cumulative_sums` are dynamically expanding `std::vector`. Since we know the upper bound of edges (`root_node_->GetNumEdges()`), we can pre-allocate it to avoid memory allocations and copying over elements.
**Action:** Always pre-allocate `std::vector` variables with `reserve` in inner loops / heavy methods if the capacity is known upfront to prevent dynamic resizing, especially in tree traversal and search logic.
