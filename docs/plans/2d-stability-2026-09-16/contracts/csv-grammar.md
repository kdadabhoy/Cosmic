# WO-06 restricted numeric CSV contract

Input paths are UTF-8, converted to native filesystem paths; Unicode path tests use
Omega and a CJK character. Files contain unquoted comma-separated rectangular rows.
LF/CRLF, blank/whitespace-only lines, an initial UTF-8 BOM, spaces/tabs around numeric
cells, decimal/exponent notation and finite double extremes (including representable
subnormals) are supported. Blank cells, ragged rows, trailing delimiters, trailing
junk, embedded CR/NUL, hex floats, quotes/quoted commas/newlines, NaN/Inf, overflow
and underflow outside representable nonzero double range are rejected.

Decimal syntax after trimming: `[+-]?([0-9]+(\.[0-9]*)?|\.[0-9]+)([eE][+-]?[0-9]+)?`.
Conversion uses locale-independent from_chars. Writer uses locale-independent
to_chars general format with double max_digits10 and LF. Finite nonzero doubles
round-trip bit exactly. +0/-0 are accepted; their sign is not a milestone guarantee.
No preservation of original textual formatting is promised.

The first nonblank row is data if every cell is a supported number. Otherwise it
must be a valid header row: nonempty unique names after trimming, first character
ASCII letter/underscore, printable ASCII thereafter, no comma/quote/CR/LF/tab.
`nan`, `NaN`, `inf`, `Inf`, `infinity` are reserved/rejected header names. Mixed
numeric-looking headers are rejected. An all-numeric-looking first row is data,
so it cannot represent header names. Duplicate headers, empty/header-only files
and quoted or Unicode headers are rejected. Header strings are returned as stored;
comparison/validation trims outer spaces. Unicode filenames are supported independently.

On every load failure both columns and optional headers are empty. Successful
headerless load returns empty headers, and successful data has equal column lengths.
False indicates invalid/missing/unreadable input; callers must check it. Scientific
double data is not narrowed through telemetry storage.

WriteCSV requires a nonempty valid header list matching columns and equal lengths;
zero data rows can be written (but cannot subsequently be loaded as a dataset).
All output numbers must be finite. WriteCircularBuffer additionally requires positive
capacity, 0<=count<=capacity, 0<=offset<capacity, matching buffers and nonnull pointers
to at least capacity floats; only selected samples must be finite. AppendRow requires
nonempty finite values; it does not inspect previous CSV grammar or enforce existing
column count. It stages the existing file plus the new numeric row, so its cost is
linear in prior file size; use bulk writes for large sessions.

Validation failures do not touch the target or create parent directories. Open,
directory, write, flush, close and atomic replacement failures return false and retain
the old target. Same-directory pending files are removed on normal failure. Successful
replacement publishes the complete new file. Concurrent writers to the same output
path and append interleavings are unsupported. Injection tests cover actual stream
badbit handling, partial staging, OS-outcome faults and native read-only/directory
replacement failure; no process-kill or filled user disk is counted as a pass.

This is not general quoted CSV or a MATLAB/JPL/Horizons importer. Such schemas remain
outside WO-06.
