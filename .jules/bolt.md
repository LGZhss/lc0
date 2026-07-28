## 2025-05-19 - Removed std::accumulate and std::transform for GameState methods
**Learning:** `std::accumulate` and `std::transform` create intermediate copies or lambda overhead that hurts performance in hot paths (like chess move loops). Simple loops that avoid extra layers of abstraction (`for (Move m : moves) { pos = Position(pos, m); }`) are demonstrably faster in this context.
**Action:** In high-frequency, CPU-bound paths, consider rewriting generic STL algorithm calls to simple `for` loops when dealing with object instantiations and heavy structures.
