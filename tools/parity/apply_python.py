"""Produce Smart Citizen's applied global.ini for a set of inputs (plan P5).

Runs the original Python code -- parse_ini_file, merge_sources_by_hierarchy,
the journal/frontend stamps and merge_ini_files -- the way
MainWindow.apply_to_game chains them, so the C++ ApplyService output can be
compared byte for byte. Dev-only; needs the reference checkout and PyQt6
(main_window imports it).

    python apply_python.py --reference "<Smart Citizen checkout>" \
        --cache "<...>/Smart Citizen/LIVE/cache" --user-ini "<...>/user.ini" \
        --version 9.9.9 --out global.ini
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from unittest.mock import patch

# Smart Citizen's AppSettings.ENHANCEMENTS_FILES, in its dict order.
ENHANCEMENT_FILES = [
    "ships_desc_enhancements.ini",
    "components_desc_enhancements.ini",
    "ship_weapons_desc_enhancements.ini",
    "fps_weapons_desc_enhancements.ini",
    "mission_rewards_enhancements.ini",
    "commodity_crafting_enhancements.ini",
    "journal_enhancements.ini",
    "missile_enhancements.ini",
    "medical_consumables_enhancements.ini",
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--reference", required=True, type=Path)
    ap.add_argument("--cache", required=True, type=Path)
    ap.add_argument("--user-ini", type=Path)
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    sys.path.insert(0, str(args.reference))
    from src.merger.ini_merger import merge_ini_files, merge_sources_by_hierarchy
    from src.parser.ini_parser import parse_ini_file

    with patch("src.utils.version.get_version", return_value=args.version):
        from src.gui.main_window import _stamp_frontend_version, _stamp_journal_entries

        base = args.cache / "base.ini"
        sources = {"global": parse_ini_file(base)}
        enhancements: dict[str, str] = {}
        for name in ENHANCEMENT_FILES:
            path = args.cache / name
            if path.exists():
                enhancements.update(parse_ini_file(path))
        hierarchy = ["global"]
        if enhancements:
            sources["enhancements"] = enhancements
            hierarchy.append("enhancements")
        user = parse_ini_file(args.user_ini, strip_values=False) if args.user_ini else {}
        if user:
            sources["user"] = user
        hierarchy.append("user")

        # apply_to_game: every entry with a custom value overrides.
        overrides = {k: v for k, v in user.items() if v}
        merged = merge_sources_by_hierarchy(sources, hierarchy, overrides)
        merged = _stamp_journal_entries(merged, sources["global"])
        merged = _stamp_frontend_version(merged)
        merge_ini_files(str(base), merged, str(args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
