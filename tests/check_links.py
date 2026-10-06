#!/usr/bin/env python3
"""Checks every relative link in README.md and docs/*.md: the target file must exist, and an
#anchor must match a heading, slugged the way GitHub does it. External links are not fetched."""
import pathlib
import re
import sys
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parent.parent
FILES = [ROOT / "README.md", *sorted((ROOT / "docs").glob("*.md"))]

MD_LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)\)")
HTML_LINK = re.compile(r"""(?:src|href)="([^"]+)\"""")


def without_code(text):
    """Text with fenced blocks and inline code removed, so examples are not taken for links."""
    text = re.sub(r"^```.*?^```", "", text, flags=re.M | re.S)
    return re.sub(r"`[^`\n]*`", "", text)


def slug(heading):
    """GitHub's heading anchor: lower case, punctuation and symbols dropped, spaces to hyphens."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", heading).replace("`", "").lower()
    kept = (c for c in text if c.isalnum() or c in "-_ " or unicodedata.category(c).startswith("M"))
    return "".join(kept).replace(" ", "-")


def anchors(path):
    found, seen = set(), {}
    text = re.sub(r"^```.*?^```", "", path.read_text(encoding="utf-8"), flags=re.M | re.S)
    for heading in re.findall(r"^#{1,6} +(.+?) *#*$", text, flags=re.M):
        base = slug(heading)
        count = seen.get(base, 0)
        found.add(base if count == 0 else f"{base}-{count}")
        seen[base] = count + 1
    return found


broken = []
checked = 0

for md in FILES:
    text = without_code(md.read_text(encoding="utf-8"))

    for target in MD_LINK.findall(text) + HTML_LINK.findall(text):
        if re.match(r"^[a-z]+:", target):
            continue  # External

        checked += 1
        path_part, _, anchor = target.partition("#")
        dest = (md.parent / path_part).resolve() if path_part else md

        if not dest.exists():
            broken.append(f"{md.relative_to(ROOT)}: {target} -> missing file")
        elif anchor and dest.suffix == ".md" and anchor not in anchors(dest):
            broken.append(f"{md.relative_to(ROOT)}: {target} -> no such heading")

for line in broken:
    print("BROKEN", line)

print(f"{'FAIL' if broken else 'OK'}: {checked} relative links in {len(FILES)} files, {len(broken)} broken")
sys.exit(1 if broken else 0)
