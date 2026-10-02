"""Record the enhancements generator's helper calls for tests/core/tst_generator.

Runs Smart Citizen's own generator test files under pytest with a plugin
that wraps selected functions of scripts/generate_enhancements_ini.py each
time a test loads it. Every call (arguments as bound before the call, so
in-place mutation doesn't leak in) and its result is written to
tests/fixtures/generator_calls.json; the C++ test replays them. Internal
calls are recorded too, which is the point: a synthetic forge tree run
through scan_* reaches the helpers with inputs the real data never has.

    python gen_generator_fixtures.py --reference "<Smart Citizen checkout>" \
        [--out tests/fixtures/generator_calls.json]

Dev-only; needs pytest and lxml.
"""
from __future__ import annotations

import argparse
import dataclasses
import functools
import hashlib
import importlib._bootstrap_external as bootstrap
import inspect
import json
import sys
from pathlib import Path, PurePath

from lxml import etree

# Function name -> the parameter names that are recorded (others must be
# defaults or are dropped). Keep in sync with the dispatch table in
# tests/core/tst_generator.cpp.
FUNCTIONS = (
    "append_enhancements",
    "classify_spawn_group",
    "_extract_spawn_counts",
    "_format_spawn_lines",
    "_extract_turret_info",
    "_classify_mission_engagement",
    "_route_token_role",
    "_is_route_title",
    "_title_has_route_token",
    "_size_abbreviation_overrides",
    "_derive_route_fragment",
    "_rep_reward_line",
    "enhancements_mission",
    "_build_blueprint_body_parts",
    "_name_from_blueprint_filename",
    "_rs_value_steps",
    "_format_rs_details_lines",
    "_format_rs_tag",
    "_build_mineable_rs_name_overrides",
    "_battaglia_contract_mineable_ores",
    "_craft_usage_key",
    "_build_craft_usage_legend",
    "_commodity_tag",
    "_missile_name_tag",
    "enhancements_mining_laser",
    "enhancements_salvage_tool",
    "enhancements_weapon",
    "bare_type_name_tag_lookup",
    "_synthesize_description",
    "_parse_compendium_locations",
    "_lookup_commodity_locations",
    "_strip_cig_size_prefix",
    "_pool_rank_label",
    "_normalize_commodity_name",
    "_humanize_craft_category",
    "_qd_size_range",
    "_condense_crafted_items",
    "_extract_difficulty",
    "_extract_mission_flags",
    "_extract_mission_xp",
)
MAX_PER_FUNCTION = 400
MAX_RECORD_BYTES = 200_000


class Unserializable(Exception):
    pass


def encode(v):
    if v is None or isinstance(v, (bool, int, str)):
        return v
    if isinstance(v, float):
        return {"$float": repr(v)}
    if isinstance(v, etree._Element):
        tree = v.getroottree()
        return {"$el": etree.tostring(tree.getroot(), encoding="unicode"), "path": tree.getpath(v)}
    if isinstance(v, PurePath):
        return {"$path": v.as_posix()}
    if dataclasses.is_dataclass(v) and hasattr(v, "to_dict"):
        return {"$cfg": v.to_dict()}
    if isinstance(v, (set, frozenset)):
        return {"$set": sorted(encode(x) for x in v)}
    if isinstance(v, (list, tuple)):
        return [encode(x) for x in v]
    if isinstance(v, dict):
        if all(isinstance(k, str) for k in v):
            return {"$dict": [[k, encode(x)] for k, x in v.items()]}
        return {"$items": [[encode(k), encode(x)] for k, x in v.items()]}
    raise Unserializable(type(v).__name__)


class Recorder:
    def __init__(self):
        self.calls: dict[str, list] = {name: [] for name in FUNCTIONS}
        self.seen: set[str] = set()

    def wrap(self, mod):
        for name in FUNCTIONS:
            fn = getattr(mod, name, None)
            if fn is None or getattr(fn, "_recorded", False):
                continue
            mod.__dict__[name] = self._wrapper(name, fn)

    def _wrapper(self, name, fn):
        sig = inspect.signature(fn)

        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            try:
                bound = sig.bind(*args, **kwargs)
                bound.apply_defaults()
                encoded = json.dumps({k: encode(v) for k, v in bound.arguments.items()}, ensure_ascii=False)
            except (Unserializable, TypeError):
                encoded = None
            try:
                result = fn(*args, **kwargs)
            except Exception as ex:  # recorded too: the C++ side must fail the same way
                self._add(name, encoded, {"$raises": type(ex).__name__})
                raise
            self._add(name, encoded, result)
            return result

        wrapper._recorded = True
        return wrapper

    def _add(self, name, encoded_args, result):
        if encoded_args is None or len(self.calls[name]) >= MAX_PER_FUNCTION:
            return
        try:
            encoded_result = json.dumps(encode(result), ensure_ascii=False)
        except Unserializable:
            return
        if len(encoded_args) + len(encoded_result) > MAX_RECORD_BYTES:
            return
        digest = hashlib.sha1(f"{name}\0{encoded_args}\0{encoded_result}".encode()).hexdigest()
        if digest in self.seen:
            return
        self.seen.add(digest)
        self.calls[name].append({"args": json.loads(encoded_args), "result": json.loads(encoded_result)})


RECORDER = Recorder()


class Plugin:
    """Wraps the generator module whenever a test file execs it."""

    def pytest_configure(self, config):
        original = bootstrap.SourceFileLoader.exec_module

        def exec_module(loader, module):
            original(loader, module)
            if Path(getattr(module, "__file__", "") or "").name == "generate_enhancements_ini.py":
                RECORDER.wrap(module)

        bootstrap.SourceFileLoader.exec_module = exec_module


def main() -> int:
    import pytest

    ap = argparse.ArgumentParser()
    ap.add_argument("--reference", required=True, type=Path)
    ap.add_argument("--out", type=Path,
                    default=Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "generator_calls.json")
    args = ap.parse_args()

    tests = sorted(p for p in (args.reference / "tests").glob("test_*.py")
                   if "generate_enhancements_ini" in p.read_text(encoding="utf-8"))
    sys.path.insert(0, str(args.reference))
    code = pytest.main(["-q", "-p", "no:cacheprovider", "--rootdir", str(args.reference),
                        *map(str, tests)], plugins=[Plugin()])
    if code not in (0, 1):
        return int(code)
    calls = {k: v for k, v in RECORDER.calls.items() if v}
    args.out.write_text(json.dumps(calls, ensure_ascii=False, indent=0) + "\n", encoding="utf-8", newline="\n")
    print(f"{sum(map(len, calls.values()))} calls from {len(calls)} functions -> {args.out}")
    for name in FUNCTIONS:
        print(f"  {name}: {len(RECORDER.calls[name])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
