## 2024-07-02 - Vector Reallocations
**Learning:** Found an opportunity to pre-calculate size and use a preallocated vector instead of `push_back` loop in `GetPositionHistoryAtNode`.
**Action:** When gathering items across linked-list like structures (e.g. tree paths to root), always count first to allocate precisely rather than dynamically appending, especially for hot search paths.
