## 2024-07-13 - [Reserve vectors dynamically based on iteration context]
**Learning:** The MCTS gathering loops use hardcoded values (like 30) to reserve vector capacity. For tree search with batching, dynamic values like `collision_limit` are better capacity estimates.
**Action:** Whenever allocating collections for batched items, use the batch limit/collision limit to prevent reallocation overheads while also avoiding arbitrarily large vector capacities.
