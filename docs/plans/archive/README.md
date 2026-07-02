# Archived Plans — Completed Work, Kept as Records

Everything here **shipped** (verified 2026-07-01) and is retained as the record of *what was
changed and why*. Do not execute items from these docs — the live plans are one level up in
[`docs/plans/`](../). Relative links inside archived docs may be stale (files were written from
`docs/plans/`); git history has the originals.

| Doc | What it was | Outcome |
| --- | --- | --- |
| [`01-bug-audit.md`](01-bug-audit.md) | Hand-verified engine bug audit (2026-07-01) | All §1 bugs fixed via doc 02's work orders |
| [`02-bugfix-ai-gameplan.md`](02-bugfix-ai-gameplan.md) | Paste-ready work orders WO-1…16 | All landed: fixes + `tests/` harness + CI + clang-tidy |
| [`06-readme-update-plan.md`](06-readme-update-plan.md) | README corrections + missing sections | A1–A4, B1–B5 done. Leftovers (B6, C split) carried into the live [`06-docs-plan.md`](../06-docs-plan.md) |
| [`07-installer-packaging-plan.md`](07-installer-packaging-plan.md) | `--project` flag, versioning, `user://`, Inno installer | Steps 1–5 shipped. Step 6 leftovers (CI release, signing, `--replay` assoc) tracked in the roadmap's continuous section. Usage: [`docs/installer-guide.md`](../../installer-guide.md) |

Superseded (deleted, not archived — replaced by full rewrites; see git history):
`03-uav-sim-engine-features.md` → [`03-simulation-engine-plan.md`](../03-simulation-engine-plan.md) ·
`04-uav-sim-app-plan.md` → [`04-viper-sim-plan.md`](../04-viper-sim-plan.md) ·
the pre-2026-07-01 versions of `00-MASTER-ROADMAP.md` and `05-3d-engine-plan.md`.
