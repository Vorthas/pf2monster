# Monster Builder for Pathfinder 2e (pf2monster)

A native Qt6 / C++20 desktop version of https://monster.pf2.tools/, a
Pathfinder 2e creature builder. Not started yet: **read
`PF2eMonsterBuilderPlan.txt` first**; it holds the features, the decisions
already made, the milestones (M1-M9 plus later items) and what to reuse.
`PF2MonsterBuilderWebsite.png` is a screenshot of the website being copied.

Names: application "Monster Builder for Pathfinder 2e", binary/CMake target
`pf2monster`, repository `PF2MonsterBuilder`, this folder
`PF2eMonsterBuilderNative`. Nothing may imply it's official or from Paizo or
pf2.tools. English only. GPL-3.0-or-later, author handle "Vorthas"; every
source file starts with:

    // SPDX-License-Identifier: GPL-3.0-or-later
    // Copyright (C) 2026 Vorthas

## The sister project: pf2scribe

`../PF2eScribeNative/` (added to sessions with `--add-dir`) is pf2scribe,
"Scribe for Pathfinder 2e": a native Qt6/C++20 version of scribe.pf2.tools by
the same user. Its `src/Rendering/` renders Paizo-style pages from Scribe
markdown. The core idea here: turn a monster into a pf2scribe `item( ... )`
block and render it with that code, which gives the preview, PNG/PDF export
and the Scribe markdown export from one path. Plan section 4 lists what to
copy. Code is COPIED into this project (decided); a shared library is a
possible later refactor across both projects, not now. Read pf2scribe's code
for reference freely; change pf2scribe only if the user asks.

## How the user works

- One feature at a time, each tested before the next. When given a task,
  they often want it planned first and clarifying questions asked before any
  code ("read through and ask me for clarification").
- They keep a Todo list (in pf2scribe it's `tools/Todo.txt`; they rewrite
  it between rounds, so re-read it each session). Notes for Claude go in
  `tools/` (e.g. a measurement log), not in temporary folders.
- The user writes README.md themselves: suggest lines, don't edit it.
- Keep the app lightweight: a little extra memory for responsiveness is
  fine, gigabytes (Electron-style) are not. Quote measured numbers when
  proposing trade-offs.
- Match the website closely ("very similar, not 100%"), but the user wants
  some deliberate improvements over it, e.g. statblocks on a white
  background, creatures up to level 25, adjustments that stack.
- Never push to GitHub (or anywhere) without the user saying so.
- No surprise deletions: avoid `rm` on anything of theirs; write test output
  to fresh folders instead.
- Tests must never touch the user's real settings: run test harnesses with
  `XDG_CONFIG_HOME` pointing at a scratch folder and an organization name
  like "PF2eMonsterBuilderTest".
- Don't add, remove or rearrange menus or options beyond what was asked.
- Rules content they will supply when the work starts. Don't scrape it:
  - Adjustments and templates (Elite, Weak, Mythic, Ghost, Lich, ...): ask
    the user for each one's exact rules when M8 begins.
  - Class roadmaps (later): ask the user; they will write the newer classes.

## Rules data and licensing

- Benchmark tables come from GM Core "Building Creatures" on Archives of
  Nethys (Paizo's official rules site, ORC license):
  https://2e.aonprd.com/Rules.aspx?ID=2874 . The tables stop at level 24;
  level 25 is extrapolated (plan section 2 has calibration data from four
  level-25 creatures). Re-read numbers from the page itself when typing
  tables in.
- Nothing from pf2.tools' own files (data.js, CSS, ...): it has no published
  license, so it is all rights reserved. Reading it to understand features
  or the website's JSON format (for import/export compatibility) is fine;
  copying it is not.
- Fonts: the same as pf2scribe (see its `fonts/README.txt`). Commercial
  fonts (Sabon, FF Good Pro, Gin, Taroca, Sofachrome, Pathfinder-Icons) never
  go in the repository; free fonts go in `fonts/free/` with one OFL license
  file per family. pf2scribe's CMake option `SCRIBE_EMBED_ALL_FONTS` (off by
  default: only fonts/free embedded) is the pattern to copy.
- Credit Paizo's ORC notice for GM Core content in the app/README.

## Technical lessons from pf2scribe (they apply to the copied code)

- Qt font engines and FreeType are per thread. Layouts built on a worker
  thread keep that thread's font engines when painted on the GUI thread. So:
  one long-lived build thread (QThreadPool max 1, expiry -1), never let it
  exit while layouts exist, and take `textEngineMutex()` (a fair lock in
  PageLayout) around anything that reads or paints a background-built
  layout. PDF export builds on the GUI thread. Breaking this caused
  segfaults in FreeType.
- Qt lays out a QTextDocument lazily (about the first 1000 characters, the
  rest on a timer that never runs here). Any edit after setTextWidth, or any
  geometry read, needs `doc.documentLayout()->frameBoundingRect(rootFrame)`
  first, or the end of long text silently disappears. Moving pf2scribe's
  tab-stop edit before that call was tried and changed the output (213
  pages became 209): leave Theme::finishLayout's order alone.
- Font files with bad metadata: FF Good Pro's files all claim regular weight
  and its italics lack the italic flag, so Qt slants them twice.
  `hideBrokenInstalledFonts()` (before QApplication) and `fixStyleMetadata()`
  in Fonts.cpp repair this; keep them.
- Images: pf2scribe's ImageCache decodes and tints each file once, shared by
  every layout, decodes in parallel and trims idle images to 128 MB.
- ThreadSanitizer reports hand-offs through Qt's own thread pool and mutexes
  as races (Qt isn't built with TSan); judge reports in our code only, and
  prefer std::mutex in new code so TSan can see it.

## Testing approach (worked well in pf2scribe)

- Small offscreen harnesses (`QT_QPA_PLATFORM=offscreen`) linking the app's
  sources, kept in a scratch folder.
- Before/after render checks: hash every page image and compare; for text
  changes also check all the text is present (pdftotext), since a pixel
  baseline can share a bug.
- Run renders from the project root so `./fonts` loads.
- Measure performance with real data (build time, time until shown, VmRSS /
  VmHWM from /proc/self/status) and log results with dates.

## Before starting

The user planned to download their monsters from monster.pf2.tools as JSON
(a backup and test files for importing) and a few PNG/PDF exports from the
site as visual references. Ask for them when M2 (statblock) and M6 (import)
come up. `PF2MonsterBuilderWebsite.png` and other screenshots should be kept
out of the repository (.gitignore) like pf2scribe does with root images.
