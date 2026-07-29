## 2025-02-12 - Code formatting vs. CI checks
**Learning:** Sometimes formatting tools like clang-format will reformat extensive portions of legacy code. If strict linters (like SonarCloud) are running and checking "New Code" density/reliability, it can incorrectly flag these newly-formatted legacy lines as "new" bugs or code smells, causing CI to fail.
**Action:** Do not arbitrarily run clang-format or other large-scale formatters unless explicitly required, to avoid triggering CI failures on legacy code analysis.
