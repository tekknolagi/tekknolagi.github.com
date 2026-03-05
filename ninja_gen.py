#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "python-liquid2>=0.3",
#     "mistletoe>=1.5",
#     "pyyaml>=6",
#     "pygments>=2",
# ]
# ///
"""Ninja-based build driver for the static site.

Generation mode (default):
  python3 ninja_gen.py              # generate build.ninja and run ninja
  python3 ninja_gen.py --gen-only   # generate build.ninja only (don't run ninja)

Build-step modes (invoked automatically by the generated ninja rules):
  python3 ninja_gen.py render-post       <post_path> <html_out> <content_out>
  python3 ninja_gen.py render-coll-item  <item_path> <html_out>
  python3 ninja_gen.py collect-posts-data <json_out> [<content1.json> ...]
  python3 ninja_gen.py render-page       <page_path> <posts_data_json> <html_out>
  python3 ninja_gen.py render-asset-page <page_path> <posts_data_json> <html_out>
  python3 ninja_gen.py copy-static       <stamp_out>

Dependency graph
----------------
  _posts/*.md          ─┐
                        ├─ render-post ──► _site/blog/slug/index.html
                        │                 _build/post-content/slug.json
                        │
  _build/post-content/  ─── collect-posts-data ──► _build/posts-data.json
                        │
  feed.xml etc.        ─┼─ render-page ──► _site/feed.xml  (needs post.content)
  blog.md etc.         ─┘                  _site/blog/index.html
                        │
  assets/rss.xsl etc.  ─── render-asset-page ──► _site/assets/rss.xsl
                        │
  assets/ demos/ etc.  ─── copy-static ──► _build/copy_static.stamp
"""

from __future__ import annotations

import datetime
import io
import json
import os
import shutil
import sys
from typing import Any

import yaml

# ---------------------------------------------------------------------------
# Bootstrap: import rendering helpers from build.py (same directory).
# ---------------------------------------------------------------------------
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import build as _b  # noqa: E402
from ninja_syntax import Writer  # noqa: E402  (vendored from upstream ninja)

SRC = _HERE
BUILD_DIR = os.path.join(SRC, "_build")
DEST = os.path.join(SRC, "_site")

# ---------------------------------------------------------------------------
# Shared helper: load the full site context
# ---------------------------------------------------------------------------


def _load_site_context() -> dict[str, Any]:
    """Load config, posts, collections, pages, layouts and a Liquid environment."""
    cfg_path = os.path.join(SRC, "_config.yml")
    site_cfg: dict = (
        yaml.safe_load(_b.read_file(cfg_path)) if os.path.isfile(cfg_path) else {}
    )
    permalink: str = site_cfg.get("permalink", "/blog/:slug/")

    layouts = _b.load_layouts(os.path.join(SRC, "_layouts"), permalink)

    posts = _b.load_posts(os.path.join(SRC, "_posts"), permalink)
    now = datetime.datetime.now()
    if not site_cfg.get("future", False):
        posts = [p for p in posts if p["fm"]["date"] <= now]

    collections_cfg: dict = site_cfg.get("collections", {})
    collections: dict = {}
    for coll_name, coll_opts in collections_cfg.items():
        if not isinstance(coll_opts, dict):
            continue
        coll_dir = os.path.join(SRC, f"_{coll_name}")
        coll_permalink = coll_opts.get("permalink", f"/{coll_name}/:path/")
        collections[coll_name] = _b.load_collection(coll_dir, coll_permalink)

    pages = _b.load_pages(SRC, site_cfg.get("exclude", []))
    site = _b.build_site_data(site_cfg, posts, collections, pages)

    includes_dir = os.path.join(SRC, "_includes")
    env = _b.make_liquid_env(includes_dir, site_cfg)

    return {
        "site_cfg": site_cfg,
        "permalink": permalink,
        "layouts": layouts,
        "posts": posts,
        "collections": collections,
        "collections_cfg": collections_cfg,
        "pages": pages,
        "site": site,
        "env": env,
    }


# ---------------------------------------------------------------------------
# Build-step implementations
# ---------------------------------------------------------------------------


def step_render_post(post_path: str, html_out: str, content_out: str) -> None:
    """Render one post → final HTML and a JSON file with just the post body HTML.

    The body HTML (before layout wrapping) is stored in *content_out* so that
    the ``collect-posts-data`` step can later make it available to pages that
    reference ``post.content`` (e.g. feed.xml).
    """
    ctx = _load_site_context()
    abs_post = os.path.abspath(post_path)
    post = next(
        (p for p in ctx["posts"] if os.path.abspath(p["path"]) == abs_post),
        None,
    )
    if post is None:
        print(f"ERROR: post not found: {post_path}", file=sys.stderr)
        sys.exit(1)

    final, body_html = _b.render_content(
        ctx["env"], post, ctx["site"], ctx["layouts"], is_markdown=True
    )
    _b.write_file(html_out, final)

    os.makedirs(os.path.dirname(os.path.abspath(content_out)), exist_ok=True)
    with open(content_out, "w", encoding="utf-8") as f:
        json.dump({"url": post["fm"]["url"], "content": body_html}, f)
    print(f"  post: {html_out}")


def step_render_coll_item(item_path: str, html_out: str) -> None:
    """Render one collection item → final HTML."""
    ctx = _load_site_context()
    abs_item = os.path.abspath(item_path)
    item = None
    for coll_items in ctx["collections"].values():
        for it in coll_items:
            if os.path.abspath(it["path"]) == abs_item:
                item = it
                break
        if item:
            break

    if item is None:
        print(f"ERROR: collection item not found: {item_path}", file=sys.stderr)
        sys.exit(1)

    final, _ = _b.render_content(
        ctx["env"], item, ctx["site"], ctx["layouts"], is_markdown=True
    )
    _b.write_file(html_out, final)
    print(f"  coll item: {html_out}")


def step_collect_posts_data(json_out: str, content_files: list[str]) -> None:
    """Merge per-post content JSON files → a single posts-data.json.

    The output preserves the canonical post order (newest-first) so that
    downstream pages see ``site.posts`` in the same order as the full build.
    Content is keyed by URL, so the order is determined by re-loading the
    site configuration rather than by the order of *content_files*.
    """
    content_by_url: dict[str, str] = {}
    for f in content_files:
        with open(f, encoding="utf-8") as fp:
            data = json.load(fp)
        content_by_url[data["url"]] = data["content"]

    ctx = _load_site_context()
    ordered = [
        {"url": post["fm"]["url"], "content": content_by_url.get(post["fm"]["url"], "")}
        for post in ctx["posts"]
    ]

    os.makedirs(os.path.dirname(os.path.abspath(json_out)), exist_ok=True)
    with open(json_out, "w", encoding="utf-8") as f:
        json.dump(ordered, f)
    print(f"  posts data: {json_out}")


def _inject_post_content(site: dict, posts_data_json: str) -> None:
    """Populate site['posts'][i]['content'] from a pre-built JSON file."""
    if not posts_data_json or not os.path.isfile(posts_data_json):
        return
    with open(posts_data_json, encoding="utf-8") as f:
        posts_data = json.load(f)
    content_by_url = {d["url"]: d["content"] for d in posts_data}
    for post in site["posts"]:
        url = post.get("url", "")
        if url in content_by_url:
            post["content"] = content_by_url[url]


def step_render_page(page_path: str, posts_data_json: str, html_out: str) -> None:
    """Render one root-level page, injecting pre-computed post content."""
    ctx = _load_site_context()
    _inject_post_content(ctx["site"], posts_data_json)

    abs_page = os.path.abspath(page_path)
    item = next(
        (p for p in ctx["pages"] if os.path.abspath(p["path"]) == abs_page),
        None,
    )
    if item is None:
        print(f"ERROR: page not found: {page_path}", file=sys.stderr)
        sys.exit(1)

    final, _ = _b.render_content(
        ctx["env"], item, ctx["site"], ctx["layouts"], item["ext"] == ".md"
    )
    _b.write_file(html_out, final)
    print(f"  page: {html_out}")


def step_render_asset_page(
    page_path: str, posts_data_json: str, html_out: str
) -> None:
    """Render one asset page (e.g. assets/rss.xsl), injecting post content."""
    ctx = _load_site_context()
    _inject_post_content(ctx["site"], posts_data_json)

    abs_page = os.path.abspath(page_path)
    asset_pages = _b.load_asset_pages(SRC)
    item = next(
        (p for p in asset_pages if os.path.abspath(p["path"]) == abs_page),
        None,
    )
    if item is None:
        print(f"ERROR: asset page not found: {page_path}", file=sys.stderr)
        sys.exit(1)

    final, _ = _b.render_content(
        ctx["env"], item, ctx["site"], ctx["layouts"], is_markdown=False
    )
    _b.write_file(html_out, final)
    print(f"  asset page: {html_out}")


def step_copy_static(stamp_out: str) -> None:
    """Copy static directories/files to _site and write a stamp file when done."""
    _b.copy_static(SRC, DEST)
    stamp_dir = os.path.dirname(os.path.abspath(stamp_out))
    if stamp_dir:
        os.makedirs(stamp_dir, exist_ok=True)
    with open(stamp_out, "w", encoding="utf-8") as f:
        f.write("")
    print(f"  static files → {stamp_out}")


# ---------------------------------------------------------------------------
# Ninja file generation
# ---------------------------------------------------------------------------


def _rel(path: str) -> str:
    """Return *path* relative to SRC (for use in the ninja file)."""
    return os.path.relpath(os.path.abspath(path), SRC).replace("\\", "/")


def generate_ninja(w: Writer) -> None:
    """Write a complete ``build.ninja`` to *w* using the vendored Writer API."""
    cfg_path = os.path.join(SRC, "_config.yml")
    site_cfg: dict = (
        yaml.safe_load(_b.read_file(cfg_path)) if os.path.isfile(cfg_path) else {}
    )
    permalink: str = site_cfg.get("permalink", "/blog/:slug/")

    posts = _b.load_posts(os.path.join(SRC, "_posts"), permalink)
    now = datetime.datetime.now()
    if not site_cfg.get("future", False):
        posts = [p for p in posts if p["fm"]["date"] <= now]

    collections_cfg: dict = site_cfg.get("collections", {})
    collections: dict = {}
    for coll_name, coll_opts in collections_cfg.items():
        if not isinstance(coll_opts, dict):
            continue
        coll_dir = os.path.join(SRC, f"_{coll_name}")
        coll_permalink = coll_opts.get("permalink", f"/{coll_name}/:path/")
        collections[coll_name] = _b.load_collection(coll_dir, coll_permalink)

    pages = _b.load_pages(SRC, site_cfg.get("exclude", []))
    asset_pages = _b.load_asset_pages(SRC)

    script = _rel(__file__)
    # These files, when changed, invalidate every build edge.
    common_deps: list[str] = [script, "build.py", "_config.yml"]

    # -----------------------------------------------------------------------
    # Header
    # -----------------------------------------------------------------------
    w.comment("Generated by ninja_gen.py — do not edit by hand.")
    w.comment("Regenerate with:  python3 ninja_gen.py --gen-only")
    w.newline()
    w.variable("builddir", "_build")
    w.newline()

    # -----------------------------------------------------------------------
    # Rules
    # -----------------------------------------------------------------------
    w.comment(
        "Render one post to HTML. Also writes a small JSON file with the"
        " rendered post body (before layout) for use by feed.xml etc."
    )
    w.rule(
        "render_post",
        command=f"python3 {script} render-post $in $html_out $content_out",
        description="Render post $in",
    )
    w.newline()

    w.comment("Render one collection item to HTML.")
    w.rule(
        "render_coll_item",
        command=f"python3 {script} render-coll-item $in $out",
        description="Render collection item $in",
    )
    w.newline()

    w.comment(
        "Merge all per-post content JSON files into a single posts-data.json."
        " Pages that need post.content (e.g. feed.xml) depend on this target."
    )
    w.rule(
        "collect_posts_data",
        command=f"python3 {script} collect-posts-data $out $in",
        description="Collect posts data -> $out",
    )
    w.newline()

    w.comment(
        "Render a root-level page. The posts_data variable points to the"
        " pre-built posts-data.json so that post.content is available."
    )
    w.rule(
        "render_page",
        command=f"python3 {script} render-page $in $posts_data $out",
        description="Render page $in",
    )
    w.newline()

    w.comment("Like render_page but for files under assets/ that contain frontmatter.")
    w.rule(
        "render_asset_page",
        command=f"python3 {script} render-asset-page $in $posts_data $out",
        description="Render asset page $in",
    )
    w.newline()

    w.comment(
        "Copy static directories (assets/, demos/, etc.) to _site/."
        " Produces a stamp file so other edges can depend on this step."
    )
    w.rule(
        "copy_static",
        command=f"python3 {script} copy-static $out",
        description="Copy static files",
    )
    w.newline()

    # -----------------------------------------------------------------------
    # Build edges — posts
    # -----------------------------------------------------------------------
    w.comment("Posts")
    w.newline()

    content_outs: list[str] = []
    all_targets: list[str] = []

    for post in posts:
        post_path = _rel(post["path"])
        slug = _b.slug_from_post_filename(post_path)
        html_out = "_site/" + _b.output_path_for_url(post["fm"]["url"]).replace(
            "\\", "/"
        )
        content_out = f"_build/post-content/{slug}.json"
        w.build(
            outputs=html_out,
            rule="render_post",
            inputs=post_path,
            implicit=common_deps,
            implicit_outputs=content_out,
            variables={"html_out": html_out, "content_out": content_out},
        )
        content_outs.append(content_out)
        all_targets.append(html_out)

    w.newline()

    # -----------------------------------------------------------------------
    # Build edges — collect posts data
    # -----------------------------------------------------------------------
    w.comment("Collect post bodies into a single JSON for pages that need post.content")
    w.newline()
    posts_data_json = "_build/posts-data.json"
    w.build(
        outputs=posts_data_json,
        rule="collect_posts_data",
        inputs=content_outs if content_outs else None,
        implicit=common_deps,
    )
    w.newline()

    # -----------------------------------------------------------------------
    # Build edges — collection items
    # -----------------------------------------------------------------------
    has_coll = any(
        collections_cfg.get(n, {}).get("output", False) for n in collections
    )
    if has_coll:
        w.comment("Collection items")
        w.newline()
        for coll_name, coll_items in collections.items():
            if not collections_cfg.get(coll_name, {}).get("output", False):
                continue
            for item in coll_items:
                item_path = _rel(item["path"])
                html_out = "_site/" + _b.output_path_for_url(
                    item["fm"]["url"]
                ).replace("\\", "/")
                w.build(
                    outputs=html_out,
                    rule="render_coll_item",
                    inputs=item_path,
                    implicit=common_deps,
                )
                all_targets.append(html_out)
        w.newline()

    # -----------------------------------------------------------------------
    # Build edges — root-level pages
    # -----------------------------------------------------------------------
    w.comment("Pages")
    w.newline()
    for page in pages:
        page_path = _rel(page["path"])
        html_out = "_site/" + _b.output_path_for_url(page["fm"]["url"]).replace(
            "\\", "/"
        )
        w.build(
            outputs=html_out,
            rule="render_page",
            inputs=page_path,
            implicit=[posts_data_json] + common_deps,
            variables={"posts_data": posts_data_json},
        )
        all_targets.append(html_out)
    w.newline()

    # -----------------------------------------------------------------------
    # Build edges — asset pages
    # -----------------------------------------------------------------------
    if asset_pages:
        w.comment("Asset pages (files under assets/ with frontmatter)")
        w.newline()
        for p in asset_pages:
            page_path = _rel(p["path"])
            out_path = "_site/" + p["fm"]["url"].lstrip("/")
            w.build(
                outputs=out_path,
                rule="render_asset_page",
                inputs=page_path,
                implicit=[posts_data_json, "_build/copy_static.stamp"] + common_deps,
                variables={"posts_data": posts_data_json},
            )
            all_targets.append(out_path)
        w.newline()

    # -----------------------------------------------------------------------
    # Build edges — static copy (stamp)
    # -----------------------------------------------------------------------
    w.comment(
        "Static files."
        " NOTE: copy_static only re-runs when ninja_gen.py, build.py, or"
        " _config.yml change, not when individual files inside assets/ change."
        " Run `ninja -B` to force a full rebuild after editing static assets."
    )
    w.newline()
    w.build(
        outputs="_build/copy_static.stamp",
        rule="copy_static",
        implicit=common_deps,
    )
    all_targets.append("_build/copy_static.stamp")
    w.newline()

    # -----------------------------------------------------------------------
    # Default target
    # -----------------------------------------------------------------------
    w.comment("Build everything by default")
    w.default(all_targets)


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------


def main() -> None:
    args = sys.argv[1:]

    # ------------------------------------------------------------------
    # Build-step dispatch (called from ninja rules)
    # ------------------------------------------------------------------
    if args and args[0] == "render-post":
        # render-post <post_path> <html_out> <content_out>
        step_render_post(args[1], args[2], args[3])
        return

    if args and args[0] == "render-coll-item":
        step_render_coll_item(args[1], args[2])
        return

    if args and args[0] == "collect-posts-data":
        # collect-posts-data <json_out> [<content_json> ...]
        step_collect_posts_data(args[1], args[2:])
        return

    if args and args[0] == "render-page":
        # render-page <page_path> <posts_data_json> <html_out>
        step_render_page(args[1], args[2], args[3])
        return

    if args and args[0] == "render-asset-page":
        step_render_asset_page(args[1], args[2], args[3])
        return

    if args and args[0] == "copy-static":
        step_copy_static(args[1])
        return

    # ------------------------------------------------------------------
    # Generation mode
    # ------------------------------------------------------------------
    gen_only = "--gen-only" in args

    print("Generating build.ninja...")
    ninja_path = os.path.join(SRC, "build.ninja")
    with open(ninja_path, "w", encoding="utf-8") as f:
        generate_ninja(Writer(f))
    print(f"Wrote {ninja_path}")

    if gen_only:
        return

    # Invoke ninja (or fall back to build.py if ninja is not installed)
    if shutil.which("ninja") is None:
        print("ninja not found in PATH — falling back to build.py")
        _b.main()
        return

    import subprocess

    print("Running ninja...")
    result = subprocess.run(["ninja"], cwd=SRC)
    if result.returncode != 0:
        sys.exit(result.returncode)


if __name__ == "__main__":
    main()
