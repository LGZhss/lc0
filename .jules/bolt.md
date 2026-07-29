## 2024-05-18 - [Beware of patch garbage]
**Learning:** Using the command-line `patch` utility can leave behind untracked `.orig` and `.rej` files when it fails. This pollutes the git working tree and staging area.
**Action:** Use safer tools like `replace_with_git_merge_diff` instead of `patch` for making targeted file changes.

## 2024-05-18 - [Random backend variance]
**Learning:** The `random` backend for `./build/release/lc0 benchmark` produces high variance in nodes/second due to its non-deterministic nature.
**Action:** Be cautious when using the `random` backend to measure the impact of small micro-optimizations, as the variance might obscure the actual performance delta.
