# Docs Plan — README Command Reference, Packaging Section, Split

> **Rewritten 2026-07-01.** The previous readme plan's corrections (A1–A4) and new sections
> (B1–B5) are all ✅ done — see [`archive/06-readme-update-plan.md`](archive/06-readme-update-plan.md)
> for the record. This doc carries the remaining documentation work as paste-ready tasks.

## D1 — Command Reference section ✅ *(added 2026-07-01 as README §1.5)* + upkeep contract

README §1.5 ("Command Reference — Every Command") documents **every command a client can run**:
all seven `.bat` scripts with arguments and examples, `CosmicApp.exe` flags, test invocations,
raw CMake configure options, and in-app hotkeys.

**The contract (enforced from now on):** any PR that adds/changes a script, an exe flag, a CMake
option, or a global hotkey must update §1.5 in the same PR. Verification sweep for any AI doing
docs work:
```
- ls *.bat at the repo root                       → every script appears in §1.5
- grep -n '"--' Runtime/Main.cpp                  → every flag appears in §1.5
- grep -n 'option(' CMakeLists.txt Cosmic/CMakeLists.txt tests/CMakeLists.txt → every option appears
- grep -n 'CS_KEY_' Cosmic/src/core/Window.cpp    → every global hotkey appears
```

## D2 — Packaging & distribution section refresh *(was B6)*

```
File: README.md §40 ("Build System" → packaging subsections). Sources: package.bat,
package_installer.bat, root CMakeLists.txt install rules, installer/CosmicSetup.iss,
docs/installer-guide.md.
Update §40 to document: package.bat's dist/<Name> layout and single-app prune mode
(package.bat SF_Telem), the projects/ scan order (LauncherLayer scans exeDir/projects then
exeDir), Release-implies-distribution (no COSMIC_DIST flag anymore — verify against
Cosmic/CMakeLists.txt before writing), package_installer.bat + Inno Setup flow, the --project
boot flag, and a link to docs/installer-guide.md for the user-facing walkthrough. Note explicitly
that COSMIC_SDK matters at BUILD time only; runtime paths are exe-relative (Runtime/Main.cpp sets CWD).
```

## D3 — Split the monolith *(was C — do LAST, one mechanical session)*

| File | Contents | Source |
| --- | --- | --- |
| `README.md` (new, ~150 lines) | what Cosmic is, screenshot, feature bullets, quickstart, **§1.5 command reference stays here**, links to the guides + docs/plans/ | write fresh |
| `docs/client-guide.md` | current §1–§29 (minus §1.5) | cut/paste |
| `docs/engine-internals.md` | current §30–§43 | cut/paste |

Rules: pure move — no rewording; fix cross-file anchors; keep §-numbers so "README §26"-style
references in code comments and plan docs stay findable; add a one-line redirect at the top of
each new file; afterwards grep the repo for `README.md#` and fix links (plans, engineering-notes,
design docs, source comments).

**Deliberately NOT recommended:** docs-site generators — three well-linked markdown files are the
right weight for a solo-dev SDK repo.

## D4 — `docs/README.md` index — S

One small index page describing the docs tree so newcomers (and AIs) navigate cold:
`plans/` (live plans + archive), `design/` (proposals), `engineering-notes/` (postmortems),
`archive/` (historical analyses), `installer-guide.md`, and — after D3 — the two guide files.

## Order

| Step | Size | Depends on |
| --- | --- | --- |
| D1 upkeep | contract, ongoing | — |
| D4 docs index | S | — |
| D2 §40 refresh | S | installer-guide.md (exists) |
| D3 split | M (mechanical) | D1/D2 done |

Verification after D3: click every TOC link on GitHub; grep for dead anchors; confirm §-number
references in source comments still resolve.
