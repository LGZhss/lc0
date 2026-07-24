## 2024-05-24 - Pre-allocating string capacity in StrJoin
**Learning:** In utility functions like `StrJoin` where multiple strings are concatenated, standard library loop concatenation causes multiple re-allocations which is slow. However, since the exact target length is determinable beforehand, we can pre-calculate it and `reserve` memory just once.
**Action:** When implementing or modifying string or container building functions, check if the final capacity can be known beforehand. If yes, compute it and call `.reserve()` to avoid allocation overhead.
