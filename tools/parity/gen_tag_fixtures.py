"""Write tests/fixtures/tag_builder.json from the original tag_builder.py.

The C++ TagBuilder test replays the same cases and must match these values.
Re-run after changing a case:

    python tools/parity/gen_tag_fixtures.py "Smart Citizen CPLusPLus"
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Cases shared with tests/core/tst_tags.cpp; keep the order in step.
TITLES = [
    "Rookie Rank - Small Cargo Haul",
    "~mission(ReputationRank) Rank - Direct ~mission(CargoGradeToken) Cargo Haul Circuit",
    "Ling Family ~mission(ReputationRank) Cargo Haul - ~mission(CargoGradeToken) Scale",
    "~mission(ReputationRank) Hauler Needed for ~mission(CargoGradeToken) Shipment",
    "Opportunity for Independent Cargo Hauler -",
    "Direct Local Shipment Route, Rank, x",
]
OPTIONS = [
    ([], "dash", False),
    (["rank", "cargo", "haul"], "pipe", False),
    (["intro", "hauler_needed_for", "local_shipment_route", "ling_family_rank", "ling_family_prefix",
      "underline_direct"], "colon", False),
    (["rank"], "space", True),
    (["cargo"], "dash", True),
]
LEGACY_BLOB = {
    "elements": [{"kind": "class", "enabled": True, "style": "long"}, {"kind": "size"}],
    "class_mapping": {"X": ["a", "b"]},
    "abbreviate_title": True,
    "placement": "weird",
    "rank_separator": "zz",
}


def main() -> int:
    sys.path.insert(0, str(Path(sys.argv[1]).resolve()))
    from src.utils.tag_builder import (CATEGORIES, TagConfig, abbreviate_title, default_config,
                                       render_route, render_tag, tag_config_fingerprint)

    out: dict = {"json": {c: default_config(c).to_json() for c in CATEGORIES}}
    defaults = {c: default_config(c) for c in CATEGORIES}
    out["fingerprint_true"] = tag_config_fingerprint(defaults, True)
    out["fingerprint_false"] = tag_config_fingerprint(defaults, False)

    render = {}
    comp = default_config("components")
    values = {"class": "Military", "size": "2", "grade": "A", "type": "Cooler"}
    render["components_default"] = render_tag(comp, values)
    comp.elements[3].enabled = True
    comp.separator, comp.enclosing = "pipe", "round"
    comp.elements[0].style, comp.elements[1].style, comp.elements[2].style = "long", "size_n", "grade_letter"
    render["components_custom"] = render_tag(comp, values)
    render["components_unknown"] = render_tag(comp, {"class": "Unknownish"})
    mis = default_config("missiles")
    render["missile_bomb"] = render_tag(mis, {"ordinance": "", "size": "2"})
    render["missile_ir"] = render_tag(mis, {"ordinance": "Infrared", "size": "1"})
    com = default_config("commodities")
    for e in com.elements:
        e.enabled = True
    render["commodity_all"] = render_tag(
        com, {"label": "Crafting", "usage": "Quantum Drive\x1fShield\x1fBogus", "collection": "Collection"})
    render["commodity_empty"] = render_tag(com, {})
    out["render"] = render

    out["abbreviate"] = [
        [abbreviate_title(t, frozenset(en), sep, st) for (en, sep, st) in OPTIONS] for t in TITLES
    ]
    out["route"] = [
        render_route("A", "B", "shape", True, False),
        render_route("A", "", "gt"),
        render_route("", "B", "to"),
        render_route("A", "B", "arrow"),
    ]
    out["legacy_from_dict"] = TagConfig.from_dict(LEGACY_BLOB).to_json()

    path = ROOT / "tests" / "fixtures" / "tag_builder.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(out, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
