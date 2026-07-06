## 2024-05-18 - Fast King Attacks
**Learning:** Replaced slow rank and file arithmetic (`std::abs(krank - rank) <= 1`) with a fast precomputed bitboard lookup (`kKingAttacks`) to optimize the highly trafficked `IsUnderAttack` function in a bitboard-centric engine.
**Action:** Use precomputed bitboards for common piece movements instead of coordinate arithmetic whenever possible to avoid branching and minimize calculations.
