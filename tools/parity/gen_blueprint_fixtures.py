"""Write tests/fixtures/blueprints.json from the original blueprint modules.

Runs owned_items.py, blueprint_meta.py and blueprint_export.py over the
Kraken global.ini shipped with Smart Citizen's tests; tests/core/
tst_blueprints.cpp replays the same work and must match. Re-run after
changing a case:

    python tools/parity/gen_blueprint_fixtures.py "Smart Citizen CPLusPLus"
"""
from __future__ import annotations

import hashlib
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Kept in step with tst_blueprints.cpp.
CONFIGS = [
    {"name": "square", "enclosings": [["[", "]"]], "bp_header": "", "stock": False},
    {"name": "mixed", "enclosings": [["", ""], ["(", ")"], ["[", "]"]], "bp_header": "MY LOOT", "stock": False},
    {"name": "stock", "enclosings": [["[", "]"]], "bp_header": "", "stock": True},
]
# StarStrings' "Ind/1/B " prefix, which the fixture's names carry.
FOREIGN_PREFIX = re.compile(r"^[A-Za-z]{2,5}/\d{1,2}/[A-Za-z]\s+")
EXPORT_TIME = datetime(2026, 4, 2, 3, 4, 5, 678000, tzinfo=timezone.utc)


def digest(lines: list[str]) -> str:
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest()


def item_line(item) -> str:
    return "\x1f".join([item.name, "\x1e".join(sorted(item.missions)), item.type or "", item.cls or "",
                        item.size or "", item.grade or "", item.tagged_name])


def main() -> int:
    sc_root = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(sc_root))
    from src.models.string_model import StringEntry
    from src.parser.ini_parser import parse_ini_file
    from src.utils import blueprint_export
    from src.utils.blueprint_meta import build_blueprint_metadata, known_item_names
    from src.utils.owned_items import (apply_owned_to_value, extract_bp_item_names, normalize_item_name,
                                       resolve_against_catalogue)

    ini = parse_ini_file(sc_root / "tests" / "fixtures" / "kraken_global_latest.ini")
    entries = [StringEntry(key=k, source_file="global", category=StringEntry.extract_category(k),
                           original_value=v, custom_value="", status="Unmodified") for k, v in ini.items()]
    stock_values = {k: FOREIGN_PREFIX.sub("", v) for k, v in ini.items()
                    if k.lower().startswith("item_name") and FOREIGN_PREFIX.match(v)}

    out: dict = {"entries": len(entries), "configs": {}}
    for cfg in CONFIGS:
        enclosings = tuple(tuple(p) for p in cfg["enclosings"])
        defaults = stock_values if cfg["stock"] else None
        meta = build_blueprint_metadata(entries, enclosings=enclosings, default_values=defaults,
                                        bp_header=cfg["bp_header"] or None)
        known = known_item_names(entries, enclosings=enclosings, default_values=defaults)
        names = sorted(meta)
        owned = set(names[::3])
        applied = []
        bullets = []
        for e in entries:
            if e.category != "Missions":
                continue
            new = apply_owned_to_value(e.original_value, owned, enclosings, cfg["bp_header"] or None)
            if new != e.original_value:
                applied.append(e.key + "=" + new)
            found = extract_bp_item_names(e.original_value, enclosings, cfg["bp_header"] or None)
            if found:
                bullets.append(e.key + "=" + "\x1e".join(sorted(found)))
        normalized = [k + "=" + normalize_item_name(v, enclosings) for k, v in ini.items()
                      if k.lower().startswith(("item_name", "vehicle_name"))]
        items = [item_line(meta[n]) for n in names]
        out["configs"][cfg["name"]] = {
            # In full once, for readable mismatches; a digest otherwise.
            "items": items if cfg["name"] == "square" else digest(items),
            "items_count": len(items),
            "known_count": len(known),
            "known": digest(sorted(known)),
            "applied_count": len(applied),
            "applied": digest(applied),
            "bullets_count": len(bullets),
            "bullets": digest(bullets),
            "normalized": digest(normalized),
        }

    # Foreign-editor recovery against the true names.
    decorated = sorted({v for k, v in ini.items() if k.startswith("item_Name") and FOREIGN_PREFIX.match(v)})
    catalogue = {FOREIGN_PREFIX.sub("", d) for d in decorated}
    resolved = [d + "=" + (resolve_against_catalogue(d, catalogue) or "<none>") for d in decorated]
    out["resolved_count"] = len(resolved)
    out["resolved"] = digest(resolved)

    meta = build_blueprint_metadata(entries)
    owned = set(sorted(meta)[::7]) | {"Not An Item", "zeta, \"quoted\""}

    class _Now:
        @staticmethod
        def now(tz=None):
            return EXPORT_TIME

    blueprint_export.datetime = _Now
    out["export_json"] = blueprint_export.export_owned_blueprints_json(owned, meta)
    out["export_csv"] = blueprint_export.export_owned_blueprints_csv(owned, meta)

    dest = ROOT / "tests" / "fixtures" / "blueprints.json"
    dest.write_text(json.dumps(out, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {dest} ({dest.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
