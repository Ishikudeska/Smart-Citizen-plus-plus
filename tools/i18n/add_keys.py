"""Add English UI strings to resources/languages/english/ui.json.

    python tools/i18n/add_keys.py strings.json

strings.json maps dotted keys to English text ({"scx.yes": "Yes"}). New
leaves are written as {"ht": text, "at": ""} like the rest of the file;
existing keys are left alone unless --replace is given. Other languages fall
back to English until translated.
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "resources/languages/english/ui.json"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("strings", type=Path)
    ap.add_argument("--replace", action="store_true")
    args = ap.parse_args()
    data = json.loads(CATALOG.read_text(encoding="utf-8"))
    added = 0
    for key, text in json.loads(args.strings.read_text(encoding="utf-8")).items():
        *sections, leaf = key.split(".")
        node = data
        for s in sections:
            node = node.setdefault(s, {})
        if leaf in node and not args.replace:
            continue
        node[leaf] = {"ht": text, "at": ""}
        added += 1
    CATALOG.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(f"added {added} strings")


if __name__ == "__main__":
    main()
