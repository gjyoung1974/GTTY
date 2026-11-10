#!/usr/bin/env python3
"""Generate the GitHub wiki pages for the gtty user manual from docs/.

usage: tools/build_wiki.py OUTDIR [--repo owner/name] [--branch main]

Each docs/*.md page becomes a wiki page named after its front-matter title
(docs/index.md becomes Home). Links between pages, images and callouts are
rewritten to what the wiki renders, and a sidebar is built from nav_order.
The .github/workflows/wiki.yml workflow pushes the result to the wiki.
"""
import argparse
import pathlib
import re
import sys

DOCS = pathlib.Path(__file__).resolve().parent.parent / "docs"
FRONT_MATTER = re.compile(r"\A---\n(.*?)\n---\n", re.S)
PAGE_LINK = re.compile(r"\]\(([A-Za-z0-9_.-]+)\.md(#[^)\s]*)?\)")
IMAGE = re.compile(r"(!\[[^\]]*\]\()(?!https?://)([^)\s]+\.(?:png|svg|jpg|jpeg|gif))\)")
CALLOUT = re.compile(r"^\{:\s*\.(\w+)\s*\}\s*$")


def read_page(path):
    text = path.read_text(encoding="utf-8")
    meta = {}
    m = FRONT_MATTER.match(text)
    if m:
        for line in m.group(1).splitlines():
            key, sep, value = line.partition(":")
            if sep:
                meta[key.strip()] = value.strip().strip("\"'")
        text = text[m.end():]
    return meta, text.lstrip("\n")


def convert(text, names, raw_base):
    out = []
    in_code = False
    callout = False  # inside the paragraph that follows a {: .note } marker
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            in_code = not in_code
        if in_code:
            out.append(line)
            continue
        m = CALLOUT.match(line)
        if m:
            callout = True
            out.append(f"> **{m.group(1).capitalize()}:**")
            continue
        if callout and not line.strip():
            callout = False

        def page(m):
            stem = m.group(1)
            if stem not in names:
                sys.exit(f"build_wiki: link to unknown page {stem}.md")
            return f"]({names[stem]}{m.group(2) or ''})"

        line = PAGE_LINK.sub(page, line)
        line = IMAGE.sub(lambda m: f"{m.group(1)}{raw_base}/{m.group(2)})", line)
        out.append("> " + line if callout else line)
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("outdir", type=pathlib.Path)
    ap.add_argument("--repo", default="gjyoung1974/GTTY")
    ap.add_argument("--branch", default="main")
    args = ap.parse_args()

    raw_base = f"https://raw.githubusercontent.com/{args.repo}/{args.branch}/docs"
    pages = []  # (nav_order, stem, title, body)
    for path in sorted(DOCS.glob("*.md")):
        meta, body = read_page(path)
        if "title" not in meta:
            continue  # not a manual page (e.g. TODO.md)
        pages.append((int(meta.get("nav_order", 999)), path.stem, meta["title"], body))
    pages.sort()

    names = {stem: "Home" if stem == "index" else re.sub(r"[^\w.-]+", "-", title).strip("-")
             for _, stem, title, _ in pages}
    if len(set(names.values())) != len(names):
        sys.exit("build_wiki: two pages have the same title")

    args.outdir.mkdir(parents=True, exist_ok=True)
    for _, stem, _, body in pages:
        (args.outdir / f"{names[stem]}.md").write_text(convert(body, names, raw_base), encoding="utf-8")

    sidebar = ["**gtty user manual**", ""]
    sidebar += [f"- [{title}]({names[stem]})" for _, stem, title, _ in pages]
    (args.outdir / "_Sidebar.md").write_text("\n".join(sidebar) + "\n", encoding="utf-8")
    (args.outdir / "_Footer.md").write_text(
        f"This wiki is generated from [`docs/`](https://github.com/{args.repo}/tree/{args.branch}/docs) "
        "in the repository. Edits made here are overwritten; change the files there instead.\n",
        encoding="utf-8")
    print(f"wrote {len(pages)} pages to {args.outdir}")


if __name__ == "__main__":
    main()
