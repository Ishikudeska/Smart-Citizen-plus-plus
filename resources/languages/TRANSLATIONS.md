# Translation Provenance

Provenance is tracked **inline** in each `languages/<lang>/ui.json` (#182). Every
leaf is an object with two fields:

```json
"apply_btn": { "ht": "Appliquer au jeu", "at": "Appliquer au jeu" }
```

- **`ht`** — the human translation. Non-empty means a human translated this key.
- **`at`** — the AI translation, used as a fallback. `tr()` shows `ht` when it is
  non-empty and falls back to `at` otherwise.

So the structure itself says who translated what:

- **`ht` non-empty** → human-translated. Never edited or overwritten by AI; only
  replaced by a better human translation.
- **`ht` empty, `at` non-empty** → no human translation yet; the app shows the AI
  string. **These are exactly the keys a human translator should review.** Find
  them by grepping a language file for `"ht": ""`.
- **both empty** → untranslated; the app falls back to the English base.

The English file is the source: every leaf is `{"ht": "<source text>", "at": ""}`
(English needs no AI fallback).

## Workflow

- **Translators:** translate a key by filling its `ht`. That immediately takes
  over from the AI `at` in the app — no list to update, the structure is the
  record. Leave `at` as-is (it stays as the safety net if `ht` is ever cleared).
- **Pre-release AI backfill:** any key still missing an `at` for an exposed
  language gets one (Claude, styled on the file's existing human strings for
  register and terminology), so no shipped language shows raw English. Seeding a
  human key's `at` from its own `ht` is fine — the known-good human text is the
  best fallback.
- The guided tour lives in `assets/tutorial.json` (English) and is translated per
  language under the `tutorial.*` keys. English has no `tutorial.*` section, so
  those keys are language-only by design.

## Needs human re-review

Keys whose **English source changed after they were human-translated**. The
`ht` values below are still the translator's words for the old English text,
so they were left untouched (AI never edits `ht`); a human should re-translate
them against the new source.

- `enhancements.apply_tag_changes_btn` — English renamed from "Apply Tag
  Changes" to "Save Tag Changes" (#214, 2.1.2). Affects french,
  portuguese_br, spanish (all three still translate "Apply...").

## Per-language notes

- **english** — source language. All strings authored by the maintainer.
- **french** — human-translated by **Akwa**, process led by **Ishikudeska**. AI
  fallbacks (Claude Opus 4.8) cover the keys whose `ht` is still empty (the tour,
  progress strings, and a handful of dialogs/config keys — grep `"ht": ""`), plus
  the `HELP.md` / `ABOUT.md` / `LEGAL.md` documents in this folder.
- **portuguese_br** — human-translated by **Nxzzin**, process led by
  **Ishikudeska**. Same AI-fallback coverage as french (grep `"ht": ""`), plus the
  `HELP.md` / `ABOUT.md` / `LEGAL.md` documents.
- **spanish** — human-translated by **Thord82**. The in-app UI strings were
  contributed as a full `ui.json` and converted to the `{ht, at}` shape (his
  strings landed in `ht`; `at` left empty). A handful of newer keys added after
  his contribution are still untranslated (grep `"ht": ""` — the simple-mode
  page, FAQ tab, a few toolbar/filter/column labels); they fall back to English
  until the pre-release AI backfill. The `HELP.md` / `ABOUT.md` / `LEGAL.md`
  documents in this folder are AI translations (2.3.0 cycle) pending Thord82's
  review. The base `global.ini` for Spanish is sourced
  from Thord82's repo (`Thord82/Star_citizen_ES`, branch `propuestas_thord`),
  which tracks the current game build far more completely than the prior Dymerz
  source (99.9% vs 78.4% key coverage). Spanish writes to the game's
  `spanish_(spain)` Localization folder with `g_language = spanish_(spain)`
  (`SC_LANGUAGE_IDS`), confirmed to render in-game.
- **italian** — AI-translated by **Claude** (#298). No human translator yet, so
  **every** key is `at`-only (`ht` empty) — the whole UI, the guided tour
  (`tutorial.*`), and the `HELP.md` / `ABOUT.md` / `LEGAL.md` / `FAQ.md`
  documents are awaiting human review (grep `"ht": ""` returns the entire
  file by design). The base `global.ini` is sourced from
  **Dymerz/StarCitizen-Localization**
  (`data/Localization/italian_(italy)/global.ini`), the same source repo
  already used for french and portuguese_br. Italian writes to the game's
  `italian_(italy)` Localization folder with `g_language = italian_(italy)`
  (`SC_LANGUAGE_IDS`). An Italian-speaking reviewer replacing the `at`
  strings with `ht` is the next step to promote it from AI-only to
  human-reviewed.
- **chinese** — AI-translated by **Claude** (#300). No human translator yet,
  so **every** key is `at`-only (`ht` empty) — the whole UI, the guided tour
  (`tutorial.*`), and the `HELP.md` / `ABOUT.md` / `LEGAL.md` / `FAQ.md`
  documents are awaiting human review (grep `"ht": ""` returns the entire
  file by design). The base `global.ini` is sourced from
  **[42Kit](https://ini.42kit.com/full/global.ini)**, a Simplified Chinese
  community translation. Chinese writes to the game's `chinese_(simplified)`
  Localization folder with `g_language = chinese_(simplified)`
  (`SC_LANGUAGE_IDS`) — a community-known value CIG doesn't officially ship
  a stock folder for, unlike the other languages here. A Chinese-speaking
  reviewer replacing the `at` strings with `ht` is the next step to promote
  it from AI-only to human-reviewed.
- **japanese** — AI-translated by **Claude** (#301). No human translator yet, so
  **every** key is `at`-only (`ht` empty) — the whole UI, the guided tour
  (`tutorial.*`), and the `HELP.md` / `ABOUT.md` / `LEGAL.md` documents are
  awaiting human review (grep `"ht": ""` returns the entire file by design).
  The base `global.ini` is sourced from **stdblue/StarCitizenJapaneseResources**
  (`v4.x/release/japanese_(japan)/global.ini`) rather than Dymerz, which does not
  ship a Japanese pack. Japanese writes to the game's `japanese_(japan)`
  Localization folder with `g_language = japanese_(japan)` (`SC_LANGUAGE_IDS`).
  A Japanese-speaking reviewer replacing the `at` strings with `ht` is the next
  step to promote it from AI-only to human-reviewed.
- **german** — AI-translated by **Claude** (#299). No human translator yet, so
  **every** key is `at`-only (`ht` empty) — the whole UI, the guided tour
  (`tutorial.*`), and the `HELP.md` / `ABOUT.md` / `LEGAL.md` / `FAQ.md`
  documents are awaiting human review (grep `"ht": ""` returns the entire
  file by design). The base `global.ini` is sourced from
  **rjcncpt/StarCitizen-Deutsch-INI** (`live/global.ini`), a community
  translation project with its own launcher and Discord. German writes to
  the game's `german_(germany)` Localization folder with
  `g_language = german_(germany)` (`SC_LANGUAGE_IDS`). A German-speaking
  reviewer replacing the `at` strings with `ht` is the next step to promote
  it from AI-only to human-reviewed.
