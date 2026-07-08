## 2024-07-28 - Pre-reserve search vectors for typical MCTS depth
**Learning:** Deep MCTS traversal dynamically allocates vectors in its hot path (e.g. `visits_to_perform`, `moves_to_path`). The default reservation of 30 was too small for deep searches, causing frequent reallocations.
**Action:** Tune initial vector reservations (like `TaskWorkspace` vectors) closer to the actual expected depth to avoid expensive allocations in the hot path.
