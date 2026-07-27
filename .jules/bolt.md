## 2024-10-24 - Prevent unnecessary copy of Move vectors
**Learning:** In highly repetitive paths like selfplay (e.g., `Evaluator::Gather`), passing a `std::vector` (like `std::vector<Move>`) by value forces an unnecessary allocation and copy on every game step, leading to measurable overhead.
**Action:** Always use `const std::vector<T>&` when passing lists or collections to functions, particularly in core loops, unless ownership specifically needs to be transferred.
