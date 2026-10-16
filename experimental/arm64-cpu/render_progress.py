#!/usr/bin/env python3
"""Generate the experiment's SVG status dashboard. Requires only Python 3."""
import argparse
import html
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parent
COLORS = {"implemented": "#31c96b", "in_progress": "#f5b83d", "planned": "#465268"}
def render(data):
    categories = data["categories"]
    done = sum(item[1] == "implemented" for cat in categories for item in cat["items"])
    total = sum(len(cat["items"]) for cat in categories)
    height = 270 + max(len(cat["items"]) for cat in categories) * 49
    lines = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1140 {height}" role="img" aria-label="AnyPS5 iOS CPU feature inventory">',
      '<rect width="100%" height="100%" fill="#101722"/>',
      '<style>text{font-family:Arial,Helvetica,sans-serif}.title{fill:#f2f7ff;font-weight:bold;font-size:32px}.sub{fill:#a6b6c9;font-size:17px}.head{fill:#e8effa;font-size:18px;font-weight:bold}.item{fill:#d7e0ec;font-size:15px}.small{fill:#a6b6c9;font-size:13px}</style>',
      '<text class="title" x="44" y="58">AnyPS5 / iOS + ARM64</text>',
      f'<text class="sub" x="44" y="91">{html.escape(data["milestone"])} · Updated {html.escape(data["updated"])}</text>',
      f'<text class="head" x="44" y="144">Feature inventory: {done} / {total} implemented</text>',
      '<text class="small" x="44" y="169">Not an emulator completion percentage</text>']
    for n, cat in enumerate(categories):
        x = 44 + n * 356
        lines += [f'<rect x="{x}" y="199" width="338" height="{height-257}" rx="12" fill="#1b2635" stroke="#35455a"/>',
                  f'<text class="head" x="{x+18}" y="235">{html.escape(cat["name"])}</text>']
        for idx, (name, state) in enumerate(cat["items"]):
            y = 269 + idx * 49
            lines += [f'<rect x="{x+18}" y="{y-15}" width="13" height="13" rx="3" fill="{COLORS[state]}"/>',
                      f'<text class="item" x="{x+40}" y="{y-3}">{html.escape(name)}</text>']
    for idx, (state, label) in enumerate([("implemented","Implemented"),("in_progress","In progress"),("planned","Planned")]):
        x = 44 + idx*170
        lines += [f'<rect x="{x}" y="{height-45}" width="12" height="12" rx="2" fill="{COLORS[state]}"/>',
                  f'<text class="small" x="{x+20}" y="{height-34}">{label}</text>']
    lines.append('</svg>')
    return "\n".join(lines) + "\n"
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    svg = render(json.loads((ROOT/"progress.json").read_text(encoding="utf-8")))
    path = ROOT/"progress.svg"
    if args.check:
        if not path.exists() or path.read_text(encoding="utf-8") != svg:
            parser.error("progress.svg out of date: run python3 render_progress.py")
    else:
        path.write_text(svg, encoding="utf-8")
