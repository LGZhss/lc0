## 2024-05-18 - Pre-allocate std::vector in Hot Loops
**Learning:** Functions called frequently during search, such as `GetBestRootChildWithTemperature` in both classic and dag implementations, allocate vectors repeatedly (`cumulative_sums`). These vectors' sizes can often be known in advance by querying properties of related objects like `root_node_->GetNumEdges()`.
**Action:** Always check if vectors in hot paths can have their memory pre-allocated via `.reserve()` using known sizes, particularly for loops iterating over node edges.
