# AP-02 execution report — 2026-09-19 (bound widgets, hosted-panel collection, interaction, goldens; V03 + V04 + V05 engine half + E05)

Status: **done**. V04 11/11 both configs; V03 7 goldens generated, reviewed by eye, compared with the
harness tolerance plus exact sentinel ROIs, Debug and Release captures byte-identical to the goldens;
E05 preview/live A/B byte-identical with identical values and confined to the widget rects with
different values; every pre-existing golden byte-identical; both configs 0 warnings; CosmicTests
498/498 (14 skipped) and CosmicRenderTests 45/45 (2 skipped) in both configs; both audits exit 0;
`ap02-units` and `ap02-gpu` manifests PASSED through the runner in both configs. Three contract
deviations recorded below (arc track geometry, plot trace geometry, placeholder label text) plus one
integration note for the host (`DrawnThisFrame`).

## Scope and provenance

- Lane worktree `C:\dev\Cosmic-ap-02`, branch `ap/02`, created with
  `git worktree add ..\Cosmic-ap-02 -b ap/02 main` from `main` at
  `e01f0a0ef0497f322307f77988f9de6bdfd8efb6` ("Merge ap/p1 …", AP-05 + AP-01 + AP-P1 landed). `main`
  did not move during the session, so the L2 `git rebase main` is a no-op (verified before landing).
- `$env:COSMIC_SDK = 'C:\dev\Cosmic-ap-02'` in every build/test shell. Nothing in `C:\dev\Cosmic`
  was edited. `engine-3d` and `cosmic-pre-2d-2026-09-16` untouched. Nothing pushed.
- Files edited are exactly the AP-02 ownership row (§10): `Cosmic/src/scene/ui/{UiComponents.h,
  UiSystem.h, UiSystem.cpp}`, `Cosmic/src/reflect/TypeRegistry.cpp`, `tests/test_ui_widgets.cpp`,
  `tests/render/render_ap02_widgets.cpp`, `tests/render/goldens/ap02_*.png`,
  `tests/acceptance/manifests/ap02-*.json`, `tests/acceptance/fixtures/Run-AP02Render.ps1`, plus the
  "May touch" TU additions to `tests/CMakeLists.txt` and `tests/render/CMakeLists.txt`, the §13 rows
  in `01-Design-Contracts.md`, and this evidence folder. No template, doc chapter or Starforge file.
- Every run in this report happened on the uncommitted lane tree (runner `results.json` records
  `commit e01f0a0, dirty true`); the engine+CMake diff at that point hashed
  `aab968d6a5fbed276e73632bb3126ff6084214fe4aefe255c5c89fe33063ead9` (`git diff HEAD -- Cosmic
  tests/CMakeLists.txt tests/render/CMakeLists.txt | sha256sum`). The commits are listed at the end.

## Toolchain and environment

- CMake 4.3.1-msvc1 (VS 18 bundled), MSVC 14.51.36231 (14.44 also installed), `-A x64
  -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON -DCOSMIC_BUILD_RENDER_TESTS=ON`, build dir
  `C:\dev\Cosmic-ap-02\build`. Effective cache: `COSMIC_2D_ONLY=ON` (compat no-op after AP-05),
  `COSMIC_BUILD_TESTS=ON`, `COSMIC_BUILD_RENDER_TESTS=ON`. Re-configured once (new test TUs).
- Windows 11 Education 10.0.26200; a real GL context is available (CosmicRenderTests ran, no
  ENVIRONMENT_BLOCKED case).
- Final clean builds after the last source change: Debug exit 0 / **0 warnings**, Release exit 0 /
  **0 warnings** (`build-debug-final.log`, `build-release-final.log`; `*.log` is gitignored, the
  counts are quoted here).

## What was built

### Components (`UiComponents.h`, §3 verbatim names/fields/defaults)

`UiValueTextComponent`, `UiGaugeComponent`, `UiIndicatorComponent`, `UiPlotComponent`,
`UiSliderComponent`, `UiToggleComponent`, `UiHostedPanelComponent`; enums `UiGaugeStyle {Bar, Arc}`,
`UiGaugeDirection {LeftToRight, BottomToTop}`, `UiSliderOrientation {Horizontal, Vertical}`; each
`CS_REGISTER_COMPONENT`ed. Runtime-only (unreflected) members: `UiSlider::Dragging` +
`DragStartValue` (the value found at press time, so Signal fires only on change), `UiToggle::Armed`,
`UiHostedPanel::DrawnThisFrame`, and `ResolvedOn/ResolvedOff` texture caches on indicator and toggle
(the same lazy-resolve pattern as `UiImage::Resolved`).

### Reflection (`TypeRegistry.cpp`)

Seven `ClassIn<…>(r, "<name>", "UI")` blocks: `AsAssetPath("texture")` on the four texture slots,
`.Color()` on every `glm::vec4` colour, `EnumValue` tables on `Style`/`Direction`/`Orientation`,
`.Range` on the bounded scalars (`StaleAfter` 0..3600, `Thickness` 0.01..1, `WindowSeconds`
0.01..3600, `GridDivisions` 0..64, `LineWidth` 0.5..32, `KnobSize` 1..256). The serializer needed no
per-type code (V04 round-trip proves it).

### UiSystem

- Pure statics `FormatValue`, `GaugeFill`, `SliderValueAt` (declared below the AP-01 signatures).
  `FormatValue` classifies the printf format: exactly one numeric conversion (`d i u o x X f F e E g G
  a A`, with `%%` not counted) prints the number (integer conversions go through `%lld`/`%llu` with a
  truncated value — never a double into `%d`); zero conversions, two or more, a non-numeric or
  malformed one (`%s`, `%*d`, dangling `%`) make the format a literal; bool prints `true`/`false`,
  string prints `AsString()`, null value prints `Placeholder`, a non-finite number prints
  `Placeholder`; `Prefix`/`Suffix` wrap every case; `stale` never changes the text.
- `CollectElements` now also lays out entities carrying any of the six drawable widget components
  (value text rides on its required sibling `UiText`).
- `Update`: topmost interactable button/slider/toggle under the pointer wins (a slider mid-drag keeps
  the pointer even off-rect); the U1 button loop is textually unchanged; sliders arm on press-inside,
  write `bus->Set(Channel, SliderValueAt(...))` every frame while dragging, end on release edge or
  `Down == false`, and emit `Signal` only when the released value differs from the press-time value;
  toggles reuse `StepButtonState` and on release-inside flip `bus->GetBool(Channel)` via `SetBool`
  and emit `Signal`. Without a bus sliders/toggles are hit-tested (they still block scene picking)
  but never write or emit. Returns true over any interactable button/slider/toggle.
- `Render` (both overloads): `bus == nullptr` ⇒ preview regardless of `preview`. Value resolution
  rule: a bus that HAS the channel is always used; otherwise live mode shows the missing state
  (Placeholder / Off / empty slider at Min / no trace) and preview mode shows `Preview*`. Stale
  colouring only in live mode (`bus->Age(ch) > StaleAfter`, `StaleAfter > 0`). Draw order per
  element: image (with button tint, or toggle On/Off tint + optional On/Off texture; an image-less
  toggle draws its own quad) → gauge → indicator → plot → slider → hosted-panel frame/placeholder →
  text (a `UiValueText` swaps the drawn string and colour through the sibling `UiText`'s font, size
  and alignment; `UiText.Text` is never written). Every draw is `Renderer2D::DrawQuad` /
  `DrawRotatedQuad` / `DrawString` (GL conformance audit clean).
- Plot: `bus->History(ch, out, WindowSeconds)`, x = time within the window, non-finite samples are
  skipped AND break the polyline (a gap, not a line across missing data), one stride-window decimation
  to ≤ 513 points = ≤ 512 segments per channel (breaks preserved), Y from the visible finite samples
  padded 5 % when `AutoScaleY` (else `YMin..YMax`; degenerate → −1..1), grid as 1-px quads,
  traces as rotated quads of `LineWidth × scale`, labels (`%g` max, `%g` min, `%gs` window) through
  `Font::Default()`. Preview: two cycles of a sine of `PreviewAmplitude` per bound channel, a
  quarter-turn phase step per channel.
- `CollectHostedPanels`: `CollectElements` filtered to `UiHostedPanel` entities (back-to-front),
  filling `{Handle, Name, Rect, Scale}` and clearing `DrawnThisFrame` (the host sets it after a
  successful `PanelRegistry::Draw`). `Render` draws the frame (`ShowFrame`, `FrameColor`, 1 px ×
  scale) and the placeholder label (`PlaceholderText` or `PanelName`) in preview mode or while
  `DrawnThisFrame == false`.

### Tests, goldens, manifests

- `tests/test_ui_widgets.cpp` — suite `AP-02 V04 widgets`, 11 cases, 366 assertions.
- `tests/render/render_ap02_widgets.cpp` — suites `AP-02 V03` (7 cases, one golden each) and
  `AP-02 E05` (1 case).
- `tests/render/goldens/ap02_{valuetext,gauge,indicator,plot,slider,toggle,hostedpanel}.png`.
- `tests/acceptance/manifests/ap02-units.manifest.json` (V04) and `ap02-gpu.manifest.json` (V03,
  E05); `tests/acceptance/fixtures/Run-AP02Render.ps1` (Run-WO08Render.ps1 with AP-02 identity; same
  H03 golden-mutation gate and diagnostics copy).

## Acceptance-case status (final binaries)

| ID | Tier | Debug | Release | Evidence |
| --- | --- | --- | --- | --- |
| V04 | U | PASSED (11 cases / 366 assertions) | PASSED (11 / 366) | `v04-{Debug,Release}/children.json`, `ap02-units-runner-*/results.json`, `suites-*-excerpts.txt` |
| V03 | G | PASSED (7 cases / 260,534 assertions; 7 goldens compared; golden dir unmutated) | PASSED (7 / 260,534) | `v03-{Debug,Release}/{children.json,goldens-sha256-before.txt,goldens-sha256-after.txt,captures/*.png}` |
| V05 (engine half) | U,G | PASSED (V04 `CollectHostedPanels` case + the `ap02_hostedpanel` case's collection and live/`DrawnThisFrame` checks) | PASSED | as above |
| E05 | G | PASSED (1 case / 527,851 assertions) | PASSED | `e05-{Debug,Release}/children.json`, `captures/ap02-e05-{preview,live-same,live-other}.png` |
| Retained: CosmicTests | U | 498 passed / 0 failed / 14 skipped (was 487 + 11 new) | 498 / 0 / 14 | `suites-*-excerpts.txt` |
| Retained: CosmicRenderTests | G | 45 passed / 0 failed / 2 skipped | 45 / 0 / 2 | `suites-*-excerpts.txt` |
| Pre-existing goldens | G | 8/8 byte-identical | 8/8 | `goldens-sha256-before.txt` vs `goldens-sha256-after.txt` (the eight non-`ap02_` rows are equal) |
| Audits | — | GL conformance exit 0; docs coverage exit 0 | same tree | `audit-gl-conformance.txt`, `audit-docs-coverage.txt` |

Runner: `Run-Acceptance.ps1 -Manifest <abs> -Config {Debug,Release} -OutDir evidence/AP-02/<manifest>-runner-<Config>`
— `ap02-units`: 1 passed / 0 failed / 0 env-blocked; `ap02-gpu`: 2 passed / 0 failed / 0 env-blocked,
both configs, exit 0 each (`ap02-*-runner-*.txt`).

### V03 goldens — what the eye sees (320×180, clear colour (18,23,33))

| Golden | Content | By eye |
| --- | --- | --- |
| `ap02_valuetext` | Three left-aligned 28 px rows. Row 1: live channel `volts = 12.345`, `%.2f`, prefix `V `, suffix ` V`, age 0. Row 2: `temp = 21.5`, `%.1f`, suffix ` C`, age 5 s > `StaleAfter 1`, `StaleColor` red. Row 3: missing channel, prefix `T `, placeholder `--`, text colour cyan. Every `UiText.Text` is "SHOULD NOT SHOW". | White "V 12.35 V", red "21.5 C", cyan "T --" (the two dashes render as one joined dash at this size). No other ink. |
| `ap02_gauge` | Bars (fill green, track blue): 0 %, 50 %, 100 % left-to-right along the top; a 20×80 bottom-to-top bar at 50 % on the left; three 60×60 arcs (thickness 0.3) at 0 %, 50 %, 100 % along the bottom. | Blue bar / half-green half-blue bar / green bar; the vertical bar green in its lower half; three rings open at the bottom: all blue, blue with the left/upper-left half green up to the top, all green. Empty ring centres. |
| `ap02_indicator` | Textured ON (left green / right yellow texture, white tint), textured OFF (grey / dark-grey texture), solid ON red, solid OFF blue. | Four flat blocks: green|yellow split, grey|dark split, red, blue. The split proves the texture (not a tint) is sampled. |
| `ap02_plot` | 300×160 plot, opaque background (0,0,51), 4×4 grid at 12 % white, channel `a` = 0.8·sin(2 cycles/10 s) yellow, channel `b` = 0.6 magenta with NaN in (3 s, 7 s), fixed Y −1..1, labels on. | Two full yellow sine cycles; a magenta horizontal line at 80 % height present in the left 30 % and right 30 % with an empty gap in the middle; faint grid; "1" top-left, "-1" bottom-left, "10s" bottom-right. |
| `ap02_slider` | Horizontal 200×30 sliders at 0, 0.5, 1 (track blue, fill green, knob 16 px white); vertical 20×160 sliders at 0, 0.5, 1. | Thin blue track with a white square knob at the left / middle (green to its left) / right (all green); three vertical tracks with the knob at the bottom / middle (green below) / top (all green). |
| `ap02_toggle` | Image+toggle ON (green tint), OFF (red tint), textured ON, textured OFF, and an image-less toggle ON (yellow). | Green, red, green|yellow split, grey|dark split blocks in the top row; a yellow block below (the fallback quad). |
| `ap02_hostedpanel` | Panel `telemetry` {20,20}-{300,125}, 1-px white frame, placeholder label; panel `x` {200,130}-{310,175} with `ShowFrame=false`, red `FrameColor`, `PlaceholderText "custom"`. | A white rectangle outline with "telemetry" centred inside; below-right the word "custom" in red with no frame. |

### Sentinel tables (all exact unless a tolerance is stated; every one PASSED in both configs)

| Golden | Pixel / ROI | Expected | Meaning |
| --- | --- | --- | --- |
| valuetext | ROI (10,10)-(310,60): ≥ 40 px within 8 of white, 0 px red | fresh row in text colour | live value, not stale |
| valuetext | ROI (10,65)-(310,115): ≥ 40 px red, 0 px white | stale row in `StaleColor` | `Age > StaleAfter` |
| valuetext | ROI (10,120)-(310,170): ≥ 10 px cyan; ROI (160,120)-(310,170): 0 ink | short placeholder row | missing channel → `Placeholder` |
| valuetext | stats: `GlyphCount == QuadCount`, ≥ 12 glyphs, 1 draw call | only text drawn | no stray widget quads |
| gauge | (11,20),(109,20) blue; (121,20),(169,20) green; **(170,20) blue**; (219,20) blue; (231,20),(309,20) green | bar fill edges at 0 / 50 / 100 % | `GaugeFill` × width, edge pixel exact |
| gauge | (20,79) blue, **(20,80) green**, (20,119) green, (20,41) blue | bottom-to-top fill edge at y = 80 | `BottomToTop` |
| gauge | ring(cx, 90°) == clear for cx ∈ {90,160,230} | the 90° gap at the bottom | 270° ring |
| gauge | 0 %: ring(90,225°), ring(90,315°) blue; 50 %: ring(160,180°), ring(160,225°) green, ring(160,315°), ring(160,0°) blue; 100 %: 225°/315°/0° green; (160,140) clear | fill sweep from bottom-left clockwise | arc fill, empty centre |
| indicator | (25,50)=(0,255,0), (75,50)=(255,255,0) ±2; (115,50)=(128,128,128), (165,50)=(64,64,64) ±2; (220,40) red; (285,35) blue; (5,5) clear; 2 indicators resolved both textures, 2 resolved none | texture halves + tints | On/Off texture + tint |
| plot | (55,42), (280,42) magenta; **(145,42) background**; ROI (110,40)-(210,45): 0 magenta | line present outside the gap, absent inside | non-finite gap |
| plot | (47,26), (122,154) yellow | sine peak / trough pixels | history mapped to x/y |
| plot | (30,120) background; (85,100).r and (200,130).r > bg+10 and < 120 | grid lines faint, elsewhere clean | grid |
| plot | ink in (11,11,30,14), (11,156,30,13), (270,156,39,13) | labels drawn | `ShowLabels` |
| plot | stats: `LineCount == 0`, `11 + 300 ≤ quads−glyphs ≤ 11 + 1024`, 2 draw calls, 1 flush | ≤ 512 segments/channel, both traces drawn, line batch unused | decimation / MaxLines |
| slider | H0 (10,25) white, (100,25),(205,25) blue; **H50 (110,65) white**, (50,65) green, (180,65) blue, (50,55) clear; H100 (209,105) white, (20,105) green | knob centre, fill, track | `Min + t(Max−Min)` |
| slider | V0 (235,165) white, (235,40) blue; **V50 (265,90) white**, (265,140) green, (265,40) blue; V100 (295,12) white, (295,100) green | vertical knob centres | `Vertical` |
| toggle | (50,30) green; (140,30) red; (200,30)/(240,30) on-texture halves ±2; (270,30)/(300,30) off-texture halves ±2; (50,80) yellow; (150,80) clear | tint into image, texture override, fallback quad | On/Off |
| hostedpanel | (20,90),(299,90),(150,20),(150,124) white; (21,21),(19,90) clear; ink in (110,62,100,20) > 20, none in (25,25,60,30); (200,150),(255,130) clear; ink in (205,135,100,35) > 10 | frame edges, label, frameless panel | §4 canvas half |
| hostedpanel | `CollectHostedPanels` → 2 entries: `telemetry` {20,20}-{300,125} then `x` at (200,130) | back-to-front, rects match the draw | V05 engine half |
| hostedpanel | live + `DrawnThisFrame = true`: frame stays, 0 label ink in both label ROIs; live + `false`: byte-identical to the preview frame | placeholder rule | §3 "host reports unregistered" |
| E05 | `BytesEqual(preview, live-same)`; `BytesEqual(preview, preview-with-bus)`; `BytesEqual(preview, preview2)` | identical values ⇒ identical bytes | preview/live parity incl. the plot's sample-exact sine replica |
| E05 | live-other vs preview: > 200 differing px, **0 outside the six bound widgets' rects**, > 0 inside each of value/gauge/indicator/toggle/slider/plot, 0 inside the hosted panel | diff confined + non-vacuous | E05 |
| E05 | gauge edge 50 % → 80 %: (100,60) green,(110,60) blue → (150,60) green,(170,60) blue; indicator (230,25) green → red; toggle (285,25) red → green; knob (105,87) → (29,87) white | each widget followed its channel | live binding |

## Non-vacuity (failing-before)

One build with all three helpers deliberately broken, V04 run, then the file restored byte-for-byte
(`failing-before-helpers-broken.txt`, 9 failed assertions in 4 cases, `Status: FAILURE!`):

| Helper | Break | Caught by |
| --- | --- | --- |
| `FormatValue` | `n == 1` → `n >= 1` (two conversions no longer literal) | `test_ui_widgets.cpp:92` `Fmt("%.1f / %.1f")` printed `3.1 / -9.2e61` (the garbage second argument) instead of the literal |
| `GaugeFill` | upper clamp removed | `:136` `GaugeFill(0,100,250) == 1` got `2.5` |
| `SliderValueAt` | `Step` ignored | `:166-171` six Step-snapping assertions (0.33≠0.25, 0.4≠0.5, 44≠40, 0.99≠0.9, 1.7≠1.5, 1.9≠2) **and** the production-path slider case `:268` (`bus.GetNumber("gain")` 0.6≠0.5) |

Golden non-vacuity: the seven `ap02_*` goldens did not exist before this lane; the comparator's
missing-golden path wrote them and FAILED (`GoldenImage.cpp:236`, 7 cases) on the generating run,
they were inspected by eye (table above), and only then did the compare run pass. During
development the plot case also caught two real defects before the goldens were accepted: NaN samples
were skipped but the polyline connected across the gap (fixed: a break point), and a background of
(0.05,0.05,0.10) rounded ambiguously on the GPU (fixture changed to an exactly representable colour).
The hosted-panel case caught the " (unregistered)" suffix breaking E05 parity (see deviations).

## Contract deviations (for AP-Q1; nothing applied to another lane's files)

1. **Arc gauge track geometry.** §3 says the track is drawn "via `Renderer2D::DrawCircle`
   thickness/fade". `DrawCircle` cannot leave the 90° gap, and the circle batch flushes AFTER the quad
   batch, so a `DrawCircle` track would paint over the quad-polyline fill. Both ring and fill are
   polylines of 48 rotated quads (chords at the mid radius, `Thickness × radius` thick, sweep 135° →
   405° with the gap at the bottom). Same look, correct painter order, no mid-pass `Flush()`.
2. **Plot trace/grid geometry.** Traces are rotated quads (`LineWidth × scale` thick, `LineWidth`
   honoured — GL line width is not available to the line batch) and the grid is 1-px quads (so it
   stays under the traces; the line batch flushes after quads). The "≤ `MaxLines` per plot" rule is
   satisfied trivially (`LineCount == 0`, pinned by V03); the ≤ 512 segments/channel decimation is
   implemented and pinned by the quad-count bound.
3. **Hosted-panel placeholder text.** §4's canvas text "`<name>` (unregistered)" is drawn as
   `PlaceholderText` (or `PanelName`) without the suffix in BOTH modes, because E05 requires
   `Render(nullptr)` and `Render(bus)` to be byte-identical while no host has reported a draw. The
   rule "placeholder when preview OR `DrawnThisFrame == false`" is kept.
4. **`FormatValue(…, stale)`** keeps the contract signature; the text is identical for stale and
   fresh (pinned), the colour carries staleness.
5. Minor additions within the lane's files: `UiSlider::DragStartValue` (runtime), `ResolvedOn/Off`
   caches on indicator/toggle (runtime), an image-less toggle draws its own quad, `Placeholder` is
   wrapped in `Prefix`/`Suffix` like a value, live-mode missing slider channel draws the knob at `Min`.
6. **Integration note (AP-01/AP-03 files, not edited here):** `PlayerLayer::DrawHostedPanels`
   ignores `m_Panels.Draw`'s result, so `UiHostedPanel::DrawnThisFrame` is never set and the live
   player shows the placeholder under the hosted ImGui window until the host writes
   `DrawnThisFrame = drawn` after `Draw` (one line; the editor half is AP-03's V05).

## Defects found

None registered. No crash, hang or data loss was found in the engine; the two plot findings above
were defects in this lane's own new code, fixed before any golden was accepted. KI register unchanged
(next free number stays KI-59).

## Measured numbers

- CosmicTests: 498 cases / 23,428,622 assertions Debug, 498 / 23,449,932 Release (0 failed, 14
  skipped, both).
- CosmicRenderTests: 45 cases / 23,516,339 assertions, 0 failed, 2 skipped, both configs.
- V03 child: 7 cases / 260,534 assertions; E05 child: 1 / 527,851; V04 child: 11 / 366 (both configs).
- Debug and Release captures of all seven widget scenes are byte-identical to the committed goldens
  (`v03-*/captures/*.png` hashes == `tests/render/goldens/ap02_*.png` hashes).
- Plot draw budget: 1 background + 10 grid + ≤ 1024 trace quads + glyphs, 1 flush, 2 draw calls.

## Caveats

- The `DrawnThisFrame` host write is pending (note 6); until AP-03/the player set it, live mode
  shows the placeholder label beneath a drawn hosted panel (`NoBackground` window). The engine
  behaviour is what §3 specifies; the host side is out of this lane's ownership.
- Inactive INDIVIDUAL UI elements (a child with `Active=false` under an active canvas) are still laid
  out and collected — pre-existing T13 behaviour (only an inactive canvas/ancestor is skipped); the
  V05 test pins the canvas rule, not a per-element rule.
- Goldens were generated on this machine's GPU in Debug; Release reproduced them byte-for-byte here.
  Other GPUs are covered by the harness tolerance (2/255, 0.1 %) plus the exact sentinel pixels,
  which sit on solid interiors, never on antialiased edges.
- The link checker (`tests/check_docs_links.ps1`, AP-D1) does not exist yet — skipped as instructed.
- `*.log` outputs of the runner children are gitignored; the excerpt files carry the summaries.

## Files

- Engine: `Cosmic/src/scene/ui/UiComponents.h`, `Cosmic/src/scene/ui/UiSystem.h`,
  `Cosmic/src/scene/ui/UiSystem.cpp`, `Cosmic/src/reflect/TypeRegistry.cpp`.
- Tests: `tests/test_ui_widgets.cpp`, `tests/render/render_ap02_widgets.cpp`,
  `tests/render/goldens/ap02_{valuetext,gauge,indicator,plot,slider,toggle,hostedpanel}.png`,
  `tests/CMakeLists.txt`, `tests/render/CMakeLists.txt`.
- Acceptance: `tests/acceptance/manifests/ap02-units.manifest.json`,
  `tests/acceptance/manifests/ap02-gpu.manifest.json`, `tests/acceptance/fixtures/Run-AP02Render.ps1`.
- Register: `docs/plans/app-platform-2026-09-18/01-Design-Contracts.md` §13 (two rows appended).
- Evidence (this folder): `report.md`, `goldens-sha256-{before,after}.txt`,
  `failing-before-helpers-broken.txt`, `suites-{Debug,Release}-excerpts.txt`,
  `audit-gl-conformance.txt`, `audit-docs-coverage.txt`, `ap02-{units,gpu}-runner-{Debug,Release}.txt`
  + `/results.json` + `/results.junit.xml`, `v04-*`, `v03-*`, `e05-*` (children.json, golden hash
  lists, captures).

## Local commits (not pushed — Kaden pushes; ap/02 is left for the integrator to merge)

1. Engine commit — the four engine files.
2. Tests commit — everything else listed above, including this report and the §13 rows.
The SHAs are printed in the session's final chat report (this file is part of commit 2).
