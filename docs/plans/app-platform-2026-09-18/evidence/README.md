# Evidence

One directory per work order (`AP-05/`, `AP-01/`, …), each with a `report.md` in the WO-10 layout, the
runner's `results.json` / `results.junit.xml`, `failing-before/` for every defect fixed, and excerpt
files. Full `*.log` files are gitignored repo-wide; commit `*-excerpts.txt` instead. Hashes of the
goldens before and after every landed merge go in `AP-Q1/golden-hashes.txt`.
