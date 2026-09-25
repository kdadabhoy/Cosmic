# Evidence

One directory per work order (`UX-01/`, `UX-02/`, …), each with a `report.md` in the WO-10 layout, the
runner's `results.json` / `results.junit.xml`, `failing-before/` for every defect fixed, and excerpt
files. Full `*.log` files are gitignored repo-wide; commit `*-excerpts.txt` instead. Guide captures land
in `docs/guides/images/` (tracked); their raw driver shots and `annotations.json` stay under the lane's
`build\_temp\` and are summarised in `UX-D1/report.md`. Hashes of the goldens before and after every landed
merge go in `UX-Q1/golden-hashes.txt`.
