#!/usr/bin/env python3
"""Minimal Jekyll-compatible static site builder.

Dependencies: python-liquid2, markdown2, pyyaml, pygments
Usage: python3 build.py
"""

import datetime
import html
import os
import re
import shutil
import sys

import markdown2
import yaml
from liquid2 import DictLoader, Environment

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SRC = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(SRC, "_site")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def read_file(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write_file(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)


def parse_frontmatter(text):
    """Return (frontmatter_dict, body) from a file with optional YAML frontmatter."""
    if not text.startswith("---"):
        return {}, text
    end = text.find("---", 3)
    if end == -1:
        return {}, text
    fm = yaml.safe_load(text[3:end]) or {}
    body = text[end + 3 :]
    if body.startswith("\n"):
        body = body[1:]
    return fm, body


def normalize_date(val):
    """Convert various date representations to a datetime object."""
    if isinstance(val, datetime.datetime):
        return val
    if isinstance(val, datetime.date):
        return datetime.datetime(val.year, val.month, val.day)
    if isinstance(val, str):
        val = val.strip()
        # Remove timezone abbreviation (e.g. "PDT", "PST")
        val = re.sub(r"\s+[A-Z]{2,4}$", "", val)
        for fmt in (
            "%Y-%m-%d %H:%M:%S",
            "%Y-%m-%d",
            "%b %d, %Y",
            "%B %d, %Y",
        ):
            try:
                return datetime.datetime.strptime(val, fmt)
            except ValueError:
                continue
    return datetime.datetime(2000, 1, 1)


def slug_from_post_filename(filename):
    """Extract slug from a post filename like 2020-08-29-my-post.md."""
    name = os.path.splitext(os.path.basename(filename))[0]
    # Strip leading date pattern YYYY-MM-DD-
    m = re.match(r"\d{4}-\d{2}-\d{2}-(.*)", name)
    return m.group(1) if m else name


def url_for_page(fm, rel_path):
    """Compute output URL for a page from its frontmatter and path."""
    if "permalink" in fm:
        return fm["permalink"]
    base = os.path.basename(rel_path)
    name, ext = os.path.splitext(base)
    if name == "index":
        return "/"
    # Non-HTML/MD files keep their extension (e.g. feed.xml)
    if ext not in (".md", ".html"):
        return f"/{base}"
    return f"/{name}/"


# ---------------------------------------------------------------------------
# Liquid setup
# ---------------------------------------------------------------------------

# Jekyll uses unquoted include names: {% include foo.md %}
# python-liquid2 requires quoted names: {% include "foo.md" %}
_INCLUDE_RE = re.compile(r"\{%[-\s]*include\s+(\S+)\s*%\}")
# {% link _posts/YYYY-MM-DD-slug.md %} (may span two lines)
_LINK_RE = re.compile(r"\{%-?\s*\n?\s*link\s+_posts/(\S+?)\.md\s*%\}")


def _link_to_url(m, permalink_pattern):
    slug = slug_from_post_filename(m.group(1) + ".md")
    return permalink_pattern.replace(":slug", slug)


def preprocess_liquid(text, page_fm, site_cfg):
    """Fix Liquid syntax differences between Jekyll and python-liquid2."""
    permalink = site_cfg.get("permalink", "/blog/:slug/")

    # Resolve {% link %} tags to URLs
    text = _LINK_RE.sub(lambda m: _link_to_url(m, permalink), text)

    # Quote include names
    def _quote_include(m):
        name = m.group(1)
        if name.startswith('"') or name.startswith("'"):
            return m.group(0)
        return '{%% include "%s" %%}' % name

    text = _INCLUDE_RE.sub(_quote_include, text)
    # Replace {% seo %} with generated HTML
    text = text.replace("{% seo %}", _seo_html(page_fm, site_cfg))
    return text


def _seo_html(page_fm, site_cfg):
    """Generate minimal SEO tags similar to jekyll-seo-tag."""
    title = page_fm.get("title", "")
    site_title = site_cfg.get("title", "")
    if title and site_title:
        full_title = f"{title} | {site_title}"
    else:
        full_title = title or site_title
    desc = page_fm.get("description", site_cfg.get("description", ""))
    url = site_cfg.get("url", "")
    canonical = page_fm.get("canonical_url", "")
    parts = [f"<title>{html.escape(full_title)}</title>"]
    if desc:
        parts.append(
            f'<meta name="description" content="{html.escape(desc)}" />'
        )
    parts.append(
        f'<meta property="og:title" content="{html.escape(full_title)}" />'
    )
    if desc:
        parts.append(
            f'<meta property="og:description" content="{html.escape(desc)}" />'
        )
    if canonical:
        parts.append(f'<link rel="canonical" href="{html.escape(canonical)}" />')
    elif url:
        page_url = page_fm.get("url", "")
        parts.append(
            f'<link rel="canonical" href="{html.escape(url + page_url)}" />'
        )
    if url:
        parts.append(f'<meta property="og:url" content="{html.escape(url)}" />')
        parts.append(
            f'<meta property="og:site_name" content="{html.escape(site_title)}" />'
        )
    return "\n    ".join(parts)


def make_liquid_env(includes_dir, site_cfg):
    """Build a liquid2 Environment with custom filters and includes."""
    # Load all includes into a dict
    includes = {}
    if os.path.isdir(includes_dir):
        for name in os.listdir(includes_dir):
            path = os.path.join(includes_dir, name)
            if os.path.isfile(path):
                includes[name] = read_file(path)

    loader = DictLoader(includes)
    env = Environment(loader=loader)

    # --- custom filters ---
    base_url = site_cfg.get("url", "")

    def xml_escape(val, *args):
        return html.escape(str(val)) if val else ""

    def absolute_url(val, *args):
        s = str(val) if val else ""
        if s.startswith("http://") or s.startswith("https://"):
            return s
        return base_url + s

    def where_exp(iterable, var_name, expression):
        """Minimal where_exp supporting 'var.attr != value' patterns."""
        if not iterable:
            return []
        expr = expression.strip()
        field = expr.replace(var_name + ".", "").split()[0] if var_name in expr else ""
        negate = "!=" in expr
        results = []
        for item in iterable:
            v = item.get(field) if isinstance(item, dict) else getattr(item, field, None)
            if negate:
                if not v:
                    results.append(item)
            else:
                if v:
                    results.append(item)
        return results

    env.filters["xml_escape"] = xml_escape
    env.filters["absolute_url"] = absolute_url
    env.filters["where_exp"] = where_exp

    return env, includes


def render_liquid(env, template_text, variables, page_fm, site_cfg):
    """Pre-process and render a Liquid template string."""
    preprocessed = preprocess_liquid(template_text, page_fm, site_cfg)
    # Also preprocess all includes
    orig_includes = dict(env.loader.templates) if hasattr(env.loader, 'templates') else {}
    new_includes = {}
    for name, src in orig_includes.items():
        new_includes[name] = preprocess_liquid(src, page_fm, site_cfg)
    env.loader = DictLoader(new_includes)
    try:
        tpl = env.from_string(preprocessed)
        return tpl.render(**variables)
    finally:
        env.loader = DictLoader(orig_includes)


def render_markdown(text):
    """Convert Markdown to HTML using markdown2."""
    return markdown2.markdown(
        text,
        extras=[
            "fenced-code-blocks",
            "footnotes",
            "tables",
            "header-ids",
            "code-friendly",
            "cuddled-lists",
            "strike",
        ],
    )


# ---------------------------------------------------------------------------
# Content loading
# ---------------------------------------------------------------------------


def _date_from_filename(fname):
    """Extract date from a post filename like 2020-08-29-my-post.md."""
    m = re.match(r"(\d{4}-\d{2}-\d{2})", fname)
    if m:
        return m.group(1)
    return None


def load_posts(posts_dir, permalink_pattern):
    """Load all posts from a directory, return list of dicts sorted newest-first."""
    posts = []
    if not os.path.isdir(posts_dir):
        return posts
    for fname in sorted(os.listdir(posts_dir)):
        if not fname.endswith(".md"):
            continue
        path = os.path.join(posts_dir, fname)
        fm, body = parse_frontmatter(read_file(path))
        if not fm:
            continue
        slug = slug_from_post_filename(fname)
        fm.setdefault("layout", "post")
        date_str = fm.get("date") or _date_from_filename(fname) or "2000-01-01"
        fm["date"] = normalize_date(date_str)
        url = permalink_pattern.replace(":slug", slug)
        fm["url"] = url
        fm["path"] = os.path.relpath(path, SRC)
        fm["slug"] = slug
        posts.append({"fm": fm, "body": body, "path": path})
    posts.sort(key=lambda p: p["fm"]["date"], reverse=True)
    return posts


def load_collection(coll_dir, permalink_pattern):
    """Load a Jekyll collection directory."""
    items = []
    if not os.path.isdir(coll_dir):
        return items
    for fname in sorted(os.listdir(coll_dir)):
        if not fname.endswith(".md"):
            continue
        path = os.path.join(coll_dir, fname)
        fm, body = parse_frontmatter(read_file(path))
        if not fm:
            continue
        name = os.path.splitext(fname)[0]
        fm["date"] = normalize_date(fm.get("date", "2000-01-01"))
        url = fm.get("permalink") or permalink_pattern.replace(":path", name)
        fm["url"] = url
        fm["path"] = os.path.relpath(path, SRC)
        items.append({"fm": fm, "body": body, "path": path})
    items.sort(key=lambda p: p["fm"]["date"])
    return items


def load_pages(src_dir, exclude):
    """Load all root-level pages (.md, .html, .xml with frontmatter)."""
    pages = []
    exclude_set = set(exclude) | {
        "README.md", "Gemfile", "Gemfile.lock", "CNAME", "Dockerfile", "LICENSE",
        "build.py", "cogapp.py", "loadstore.py", "markdown2.py", "prelude.py",
        "fly.toml", "dat.json", "new.sh",
    }
    page_exts = {".md", ".html", ".xml"}
    for fname in sorted(os.listdir(src_dir)):
        if fname.startswith(("_", ".")):
            continue
        if fname in exclude_set:
            continue
        ext = os.path.splitext(fname)[1]
        if ext not in page_exts:
            continue
        path = os.path.join(src_dir, fname)
        if not os.path.isfile(path):
            continue
        text = read_file(path)
        if not text.startswith("---"):
            continue  # No frontmatter = not a page
        fm, body = parse_frontmatter(text)
        if not fm:
            fm = {}
        fm["url"] = url_for_page(fm, fname)
        fm["path"] = fname
        pages.append({"fm": fm, "body": body, "path": path, "ext": ext})
    return pages


def load_layouts(layouts_dir):
    """Load all layout templates."""
    layouts = {}
    if not os.path.isdir(layouts_dir):
        return layouts
    for fname in os.listdir(layouts_dir):
        path = os.path.join(layouts_dir, fname)
        if os.path.isfile(path):
            name = os.path.splitext(fname)[0]
            layouts[name] = read_file(path)
    return layouts


# ---------------------------------------------------------------------------
# Building
# ---------------------------------------------------------------------------


def post_to_dict(p):
    """Convert a post/page record to a dict for Liquid templates."""
    d = dict(p["fm"])
    return d


def build_site_data(site_cfg, posts, collections, pages):
    """Build the site-level variables dict for Liquid."""
    site = dict(site_cfg)
    site["posts"] = [post_to_dict(p) for p in posts]
    site["pages"] = [post_to_dict(p) for p in pages]
    for coll_name, coll_items in collections.items():
        site[coll_name] = [post_to_dict(p) for p in coll_items]
    return site


def render_content(env, item, site, layouts, site_cfg, is_markdown):
    """Render a content item (post/page/collection item) to final HTML.

    Returns (final_html, rendered_body) where rendered_body is the content
    before layout wrapping (useful for post.content in templates).
    """
    fm = item["fm"]
    body = item["body"]

    page_vars = dict(fm)
    variables = {"site": site, "page": page_vars, "jekyll": {"environment": "development"}}

    # 1) Render Liquid in content body
    rendered_body = render_liquid(env, body, variables, fm, site_cfg)

    # 2) Convert Markdown to HTML if needed
    if is_markdown:
        rendered_body = render_markdown(rendered_body)

    # 3) Apply layout
    layout_name = fm.get("layout")
    if layout_name and layout_name != "none" and layout_name in layouts:
        layout_src = layouts[layout_name]
        variables["content"] = rendered_body
        page_vars["content"] = rendered_body
        final = render_liquid(env, layout_src, variables, fm, site_cfg)
    elif layout_name == "none":
        # Render Liquid but don't wrap in layout
        final = rendered_body
    else:
        final = rendered_body

    return final, rendered_body


def output_path_for_url(url):
    """Convert a URL like /blog/foo/ to an output file path relative to _site."""
    url = url.lstrip("/")
    if not url:
        return "index.html"
    if url.endswith("/"):
        return os.path.join(url, "index.html")
    if "." in os.path.basename(url):
        return url
    return os.path.join(url, "index.html")


def copy_static(src_dir, dest_dir):
    """Copy static files and directories to the output."""
    static_dirs = ["assets", "demos", "resources", ".well-known"]
    static_files = ["favicon.ico", "CNAME", "dat.json"]

    for d in static_dirs:
        src = os.path.join(src_dir, d)
        dst = os.path.join(dest_dir, d)
        if os.path.isdir(src):
            if os.path.exists(dst):
                shutil.rmtree(dst)
            shutil.copytree(src, dst)

    for f in static_files:
        src = os.path.join(src_dir, f)
        dst = os.path.join(dest_dir, f)
        if os.path.isfile(src):
            shutil.copy2(src, dst)

    # Handle SCSS -> CSS (strip frontmatter, copy as-is since no real SCSS features)
    scss_path = os.path.join(dest_dir, "assets", "css", "main.scss")
    css_path = os.path.join(dest_dir, "assets", "css", "main.css")
    if os.path.isfile(scss_path):
        text = read_file(scss_path)
        _, body = parse_frontmatter(text)
        write_file(css_path, body)
        os.remove(scss_path)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    print(f"Source: {SRC}")
    print(f"Destination: {DEST}")

    # Clean output
    if os.path.exists(DEST):
        shutil.rmtree(DEST)
    os.makedirs(DEST)

    # Load config
    cfg_path = os.path.join(SRC, "_config.yml")
    site_cfg = yaml.safe_load(read_file(cfg_path)) if os.path.isfile(cfg_path) else {}

    # Load layouts
    layouts = load_layouts(os.path.join(SRC, "_layouts"))

    # Load posts
    permalink = site_cfg.get("permalink", "/blog/:slug/")
    posts = load_posts(os.path.join(SRC, "_posts"), permalink)
    # Filter out future posts
    now = datetime.datetime.now()
    if not site_cfg.get("future", False):
        posts = [p for p in posts if p["fm"]["date"] <= now]

    # Load collections
    collections_cfg = site_cfg.get("collections", {})
    collections = {}
    for coll_name, coll_opts in collections_cfg.items():
        if not isinstance(coll_opts, dict):
            continue
        coll_dir = os.path.join(SRC, f"_{coll_name}")
        coll_permalink = coll_opts.get("permalink", f"/{coll_name}/:path/")
        collections[coll_name] = load_collection(coll_dir, coll_permalink)

    # Load pages
    pages = load_pages(SRC, site_cfg.get("exclude", []))

    # Build site data
    site = build_site_data(site_cfg, posts, collections, pages)

    # Set up Liquid environment
    env, _includes = make_liquid_env(os.path.join(SRC, "_includes"), site_cfg)

    # --- Render posts (first pass: get content for post.content) ---
    print(f"Rendering {len(posts)} posts...")
    for i, p in enumerate(posts):
        output, body_html = render_content(env, p, site, layouts, site_cfg, is_markdown=True)
        out_path = os.path.join(DEST, output_path_for_url(p["fm"]["url"]))
        write_file(out_path, output)
        # Store rendered content on site data so post.content works in templates
        site["posts"][i]["content"] = body_html

    # --- Render collections ---
    for coll_name, coll_items in collections.items():
        coll_opts = collections_cfg.get(coll_name, {})
        if not coll_opts.get("output", False):
            continue
        print(f"Rendering {len(coll_items)} {coll_name} items...")
        for i, item in enumerate(coll_items):
            output, body_html = render_content(env, item, site, layouts, site_cfg, is_markdown=True)
            out_path = os.path.join(DEST, output_path_for_url(item["fm"]["url"]))
            write_file(out_path, output)
            site[coll_name][i]["content"] = body_html

    # --- Render pages ---
    print(f"Rendering {len(pages)} pages...")
    for p in pages:
        is_md = p["ext"] == ".md"
        output, _ = render_content(env, p, site, layouts, site_cfg, is_markdown=is_md)
        out_path = os.path.join(DEST, output_path_for_url(p["fm"]["url"]))
        write_file(out_path, output)

    # --- Copy static files ---
    print("Copying static files...")
    copy_static(SRC, DEST)

    # Count output files
    count = sum(len(files) for _, _, files in os.walk(DEST))
    print(f"Done! Generated {count} files in {DEST}")


if __name__ == "__main__":
    main()
