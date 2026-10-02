"""Run Smart Citizen's enhancements generator for parity checks (plan P4).

Imports the original scripts/generate_enhancements_ini.py by path and calls
main() the way EnhancementsGeneratorWorker does, with the options the app
would pass for a fresh profile unless --options names a JSON file that
overrides some of them. The pickle lookup cache is bypassed so nothing is
written into the DataForge cache. Dev-only; needs lxml.

    python run_python_generator.py --reference "<Smart Citizen checkout>" \
        --base-ini "<...>/LIVE/cache/base.ini" \
        --forge-dir "<LocalAppData>/Smart Citizen/LIVE/cache/dataforge" \
        --out <scratch dir> [--options opts.json]

Options JSON keys (all optional): categories (list), tag_configs ({category:
TagConfig dict}), annotate_mission_descs, rep_xp_label, mission_headers,
mission_header_em_tag, mission_detail_fields, mission_title_tags,
stats_prepend, standardize_earnable_ship_names, rs_ore_name_annotations,
english_base_ini (path).
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import logging
import shutil
import sys
import time
from pathlib import Path

MISSION_FIELD_KEYS = ("mission_type", "difficulty", "spawns", "reputation", "blueprints", "ace",
                      "resource_signatures")
MISSION_TITLE_TAG_DEFAULTS = {"rep": True, "blueprint": True, "ace": True, "rs": True, "rep_track": False}
MISSION_HEADER_DEFAULTS = {"details": "MISSION DETAILS", "blueprints": "POTENTIAL BLUEPRINTS",
                           "items": "ITEM REWARDS", "blueprint_data": "BLUEPRINT DATA"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--reference", required=True, type=Path)
    ap.add_argument("--base-ini", required=True, type=Path)
    ap.add_argument("--forge-dir", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--options", type=Path)
    ap.add_argument("--patches", type=Path, help="defaults to the reference checkout's patches/")
    args = ap.parse_args()

    logging.basicConfig(level=logging.WARNING, format="%(levelname)s %(name)s: %(message)s")
    sys.path.insert(0, str(args.reference))
    from src.utils.tag_builder import CATEGORIES, TagConfig, default_config

    opts = json.loads(args.options.read_text(encoding="utf-8")) if args.options else {}
    tag_configs = {c: default_config(c) for c in CATEGORIES}
    for cat, blob in (opts.get("tag_configs") or {}).items():
        tag_configs[cat] = TagConfig.from_dict(blob)
    title_tags = dict(MISSION_TITLE_TAG_DEFAULTS, **(opts.get("mission_title_tags") or {}))
    detail_fields = {k: True for k in MISSION_FIELD_KEYS}
    detail_fields.update(opts.get("mission_detail_fields") or {})
    headers = dict(MISSION_HEADER_DEFAULTS, **(opts.get("mission_headers") or {}))

    # main() writes beside its base.ini, so give it a private copy.
    args.out.mkdir(parents=True, exist_ok=True)
    base_ini = args.out / "base.ini"
    shutil.copyfile(args.base_ini, base_ini)

    script = args.reference / "scripts" / "generate_enhancements_ini.py"
    spec = importlib.util.spec_from_file_location("generate_enhancements_ini_parity", script)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    mod._cached_lookup = lambda forge_dir, name, builder, extra_key="": builder()

    english = opts.get("english_base_ini")
    started = time.perf_counter()
    mod.main(base_ini, args.forge_dir,
             categories=set(opts["categories"]) if opts.get("categories") else None,
             patches_dir=args.patches or (args.reference / "patches"),
             max_workers=1,
             tag_configs=tag_configs,
             annotate_mission_descs=opts.get("annotate_mission_descs", True),
             rep_xp_label=opts.get("rep_xp_label", "Rep"),
             mission_headers=headers,
             mission_header_em_tag=opts.get("mission_header_em_tag", "EM3"),
             mission_detail_fields=detail_fields,
             mission_title_tags=title_tags,
             stats_prepend=opts.get("stats_prepend", False),
             standardize_earnable_ship_names=opts.get("standardize_earnable_ship_names", False),
             rs_ore_name_annotations=opts.get("rs_ore_name_annotations", True),
             english_base_ini_path=Path(english) if english else base_ini)
    print(f"generated in {time.perf_counter() - started:.1f}s -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
