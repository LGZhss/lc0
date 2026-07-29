## 2024-07-22 - Pre-allocating Vectors for Search Node Gathering
**Learning:** In highly recursive/iterative search functions like MCTS variants, dynamically appending to vectors (`std::vector::push_back` without `reserve()`) for structures like move histories and child edge lists can cause frequent memory reallocations, which adds up to a noticeable overhead.
**Action:** Pre-allocate memory using `reserve()` for collections where the maximum or exact size is known beforehand, particularly inside the hot loops of search trees.
