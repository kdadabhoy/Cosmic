# Static Analysis (clang-tidy)

The repo root carries a curated [`.clang-tidy`](../../.clang-tidy): `bugprone-*`,
`performance-*`, `concurrency-*`, plus a couple of low-noise modernize/readability
checks. Style-churn checks are off on purpose — the goal is bug signal, not
reformatting a working engine.

## Running it

clang-tidy ships with the "C++ Clang tools for Windows" component in the Visual
Studio installer (or LLVM's installer). Two ways to run:

**1. One-off over a file or folder (compile database route):**

```bat
cmake -S . -B build-tidy -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build-tidy Cosmic/src/serial/SerialPort.cpp
```

(The Visual Studio generator does not emit `compile_commands.json` — use a
throwaway Ninja configure as above just for analysis.)

**2. Wired into a build (slower, exhaustive):**

```bat
cmake -S . -B build-tidy -G Ninja -DCMAKE_CXX_CLANG_TIDY=clang-tidy
cmake --build build-tidy
```

## Policy

- New engine code should come back clean for `bugprone-*` and `concurrency-*`.
- Do **not** mass-fix existing findings in unrelated PRs — fix what you touch.
- If a check produces sustained noise, disable it in `.clang-tidy` with a comment
  saying why, rather than sprinkling `// NOLINT`.
