#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "python-liquid2>=0.3",
#     "mistune>=3.2",
#     "pyyaml>=6",
#     "pygments>=2",
# ]
# ///
"""Minimal Jekyll-compatible static site builder.

Usage: uv run build.py
"""

import datetime
import html
import os
import re
import shutil

import mistune
from mistune import InlineParser, Markdown
import yaml
from liquid2 import DictLoader, Environment, Node, Tag
from liquid2.builtin.expressions import (
    Path,
    StringLiteral,
    parse_keyword_arguments,
    parse_primitive,
    parse_string_or_identifier,
    parse_string_or_path,
)
from liquid2.builtin.tags.include_tag import IncludeTag
from liquid2.context import RenderContext
from liquid2.exceptions import LiquidSyntaxError
from liquid2.stream import TokenStream
from liquid2.token import TagToken, TokenType
from mistune.plugins.footnotes import footnotes
from mistune.plugins.formatting import strikethrough
from mistune.plugins.table import table
from pygments import highlight as pyg_highlight
from pygments.formatters import HtmlFormatter
from pygments.lexers import get_lexer_by_name
from pygments.util import ClassNotFound

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
    """Convert various date representations to a datetime object.

    When a timezone abbreviation is present (e.g. PST, PDT, EDT), the time
    is converted to UTC to match Jekyll's behaviour.
    """
    if isinstance(val, datetime.datetime):
        return val
    if isinstance(val, datetime.date):
        return datetime.datetime(val.year, val.month, val.day)
    if isinstance(val, str):
        val = val.strip()
        # Extract and handle timezone abbreviation (e.g. "PDT", "PST")
        tz_offsets = {
            "PST": -8, "PDT": -7,
            "MST": -7, "MDT": -6,
            "CST": -6, "CDT": -5,
            "EST": -5, "EDT": -4,
            "UTC": 0, "GMT": 0,
        }
        utc_offset = None
        tz_match = re.search(r"\s+([A-Z]{2,4})$", val)
        if tz_match and tz_match.group(1) in tz_offsets:
            utc_offset = tz_offsets[tz_match.group(1)]
            val = val[:tz_match.start()]
        for fmt in (
            "%Y-%m-%d %H:%M:%S",
            "%Y-%m-%d",
            "%b %d, %Y",
            "%B %d, %Y",
        ):
            try:
                dt = datetime.datetime.strptime(val, fmt)
                if utc_offset is not None:
                    dt -= datetime.timedelta(hours=utc_offset)
                return dt
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

# {% link _posts/YYYY-MM-DD-slug.md %} contains filesystem paths with '/'
# which the liquid2 lexer rejects.  We resolve them once at load time so that
# render_liquid never has to touch them.
_LINK_RE = re.compile(r"\{%-?\s*\n?\s*link\s+_posts/(\S+?)\.md\s*%\}")


def _resolve_link_tags(text, permalink_pattern):
    """Replace {% link _posts/... %} with the resolved URL."""
    def _repl(m):
        slug = slug_from_post_filename(m.group(1) + ".md")
        return permalink_pattern.replace(":slug", slug)
    return _LINK_RE.sub(_repl, text)


# --- Custom Liquid tags ---


class JekyllIncludeTag(IncludeTag):
    """Override include to treat unquoted names like css.md as string literals."""

    def parse(self, stream: TokenStream) -> Node:
        token = stream.current()
        assert isinstance(token, TagToken)
        if not token.expression:
            raise LiquidSyntaxError("expected template name", token=token)

        tokens = TokenStream(token.expression)
        name = parse_string_or_path(tokens.next())

        # Jekyll uses unquoted include names: {% include css.md %}
        # The lexer parses 'css.md' as a Path(['css', 'md']).
        # Convert it to a string literal so the loader can find the template.
        if isinstance(name, Path):
            name = StringLiteral(name.token, ".".join(name.path))

        loop = False
        var = None
        alias = None

        if tokens.current().type_ == TokenType.FOR and tokens.peek().type_ not in (
            TokenType.COLON,
            TokenType.COMMA,
        ):
            tokens.next()
            loop = True
            var = parse_primitive(self.env, tokens.next())
            if tokens.current().type_ == TokenType.AS:
                tokens.next()
                alias = parse_string_or_identifier(tokens.next())
        elif tokens.current().type_ == TokenType.WITH and tokens.peek().type_ not in (
            TokenType.COLON,
            TokenType.COMMA,
        ):
            tokens.next()
            var = parse_primitive(self.env, tokens.next())
            if tokens.current().type_ == TokenType.AS:
                tokens.next()
                alias = parse_string_or_identifier(tokens.next())

        args = parse_keyword_arguments(self.env, tokens)
        tokens.expect_eos()
        return self.node_class(
            token, name, loop=loop, var=var, alias=alias, args=args
        )


class SeoNode(Node):
    """Render SEO meta tags similar to jekyll-seo-tag."""

    __slots__ = ()

    def render_to_output(self, context: RenderContext, buffer) -> int:
        page = context.resolve("page", default={})
        site = context.resolve("site", default={})
        if not isinstance(page, dict):
            page = {}
        if not isinstance(site, dict):
            site = {}

        title = page.get("title", "")
        site_title = site.get("title", "")
        if title and site_title:
            full_title = f"{title} | {site_title}"
        else:
            full_title = title or site_title
        desc = page.get("description", "") or site.get("description", "")
        url = site.get("url", "")
        canonical = page.get("canonical_url", "")
        page_url = page.get("url", "")

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
            parts.append(
                f'<link rel="canonical" href="{html.escape(canonical)}" />'
            )
        elif url:
            parts.append(
                f'<link rel="canonical" href="{html.escape(url + page_url)}" />'
            )
        if url:
            parts.append(
                f'<meta property="og:url" content="{html.escape(url)}" />'
            )
            parts.append(
                f'<meta property="og:site_name" content="{html.escape(site_title)}" />'
            )
        out = "\n    ".join(parts)
        buffer.write(out)
        return len(out)


class SeoTag(Tag):
    """The {% seo %} tag."""

    block = False
    node_class = SeoNode

    def parse(self, stream: TokenStream) -> Node:
        return SeoNode(stream.current())


def make_liquid_env(includes_dir, site_cfg):
    """Build a liquid2 Environment with custom tags and filters."""
    permalink = site_cfg.get("permalink", "/blog/:slug/")

    # Load all includes into a dict, resolving {% link %} tags once.
    includes = {}
    if os.path.isdir(includes_dir):
        for name in os.listdir(includes_dir):
            path = os.path.join(includes_dir, name)
            if os.path.isfile(path):
                includes[name] = _resolve_link_tags(read_file(path), permalink)

    loader = DictLoader(includes)
    env = Environment(loader=loader)

    # --- custom tags ---
    env.tags["include"] = JekyllIncludeTag(env)
    env.tags["seo"] = SeoTag(env)

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
        """Minimal where_exp for expressions like 'post.index != true'."""
        if not iterable:
            return []
        expr = expression.strip()
        # Parse "var.field op value" from expressions like "post.index != true"
        prefix = var_name + "."
        if not expr.startswith(prefix):
            return list(iterable)
        rest = expr[len(prefix):]
        parts = rest.split()
        if len(parts) < 3:
            return list(iterable)
        field, op = parts[0], parts[1]
        val_str = parts[2]
        cmp_val = True if val_str == "true" else (False if val_str == "false" else val_str)
        results = []
        for item in iterable:
            v = item.get(field) if isinstance(item, dict) else getattr(item, field, None)
            if op == "!=" and v != cmp_val:
                results.append(item)
            elif op == "==" and v == cmp_val:
                results.append(item)
        return results

    env.filters["xml_escape"] = xml_escape
    env.filters["absolute_url"] = absolute_url
    env.filters["where_exp"] = where_exp

    return env


def render_liquid(env, template_text, variables):
    """Render a Liquid template string.

    All {% link %} tags must already be resolved before calling this.
    """
    tpl = env.from_string(template_text)
    return tpl.render(**variables)


# ---------------------------------------------------------------------------
# Markdown rendering (mistune + Pygments)
# ---------------------------------------------------------------------------

# Multi-line <span> blocks: mistune treats a <span> on its own line as
# block-level HTML and won't process markdown inside it.  Collapsing
# to a single line lets the inline parser handle the markdown content.
_INLINE_SPAN_RE = re.compile(
    r"<(span[^>]*)>\n(.*?)\n</span>", re.DOTALL
)

# Kramdown annotation that marks a heading to be excluded from the TOC.
_NO_TOC_RE = re.compile(
    r"(^#{1,6}\s+.+)\n\{:\s*\.no_toc\s*\}", re.MULTILINE
)

# Kramdown {:toc} marker (preceded by a throwaway list bullet).
_TOC_RE = re.compile(r"^\*[^\n]*\n\{:toc\}\s*$", re.MULTILINE)


# Block-level HTML tags whose children should not be treated as code.
_BLOCK_TAGS = frozenset({
    "ul", "ol", "div", "table", "section", "article", "aside",
    "header", "footer", "nav", "details", "fieldset", "figure",
    "blockquote", "form", "dl",
})


_BLOCK_OPEN_RE = re.compile(r"<(" + "|".join(_BLOCK_TAGS) + r")\b")
_BLOCK_CLOSE_RE = re.compile(r"</(" + "|".join(_BLOCK_TAGS) + r")\b")


_PYGMENTS_FORMATTER = HtmlFormatter(cssclass="highlight")


class _JekyllRenderer(mistune.HTMLRenderer):
    """Custom mistune renderer with Pygments highlighting and heading IDs.

    Collects headings during rendering so a TOC can be built afterwards
    without re-parsing the HTML.
    """

    # Regex for smart-dash conversion inside raw HTML blocks: skip
    # <pre>…</pre>, HTML comments, and individual tags.
    _BLOCK_HTML_SKIP_RE = re.compile(
        r"(<pre[\s>].*?</pre>|<!--.*?-->|<[^>]+>)", re.DOTALL
    )

    def __init__(self, **kwargs):
        super().__init__(escape=False, **kwargs)
        self.toc_items: list[tuple[int, str, str]] = []

    def block_code(self, code, info=None):
        if info:
            try:
                lexer = get_lexer_by_name(info.strip())
                return pyg_highlight(code, lexer, _PYGMENTS_FORMATTER)
            except ClassNotFound:
                pass
        return f'<div class="language-plaintext highlighter-rouge"><div class="highlight"><pre class="highlight"><code>{mistune.escape(code)}</code></pre></div></div>\n'

    def codespan(self, text):
        return f'<code class="language-plaintext highlighter-rouge">{mistune.escape(text)}</code>'

    def block_html(self, html):
        """Pass through raw HTML blocks, applying smart-dash conversion.

        The inline parser handles smart dashes for markdown text, but raw
        HTML blocks bypass inline parsing entirely.  We apply the same
        conversion here, skipping ``<pre>``, comments, and tag attributes.
        """
        parts = self._BLOCK_HTML_SKIP_RE.split(html)
        for i, part in enumerate(parts):
            if not part.startswith("<"):
                parts[i] = part.replace("---", "\u2014").replace("--", "\u2013")
        return "".join(parts) + "\n"

    def heading(self, text, level, **attrs):
        raw = re.sub(r"<[^>]+>", "", text)
        hid = re.sub(r"[^\w\s-]", "", raw).strip().lower()
        hid = re.sub(r"[\s]+", "-", hid)
        self.toc_items.append((level, hid, raw.strip()))
        return f'<h{level} id="{hid}">{text}</h{level}>\n'

    def render_toc(self, exclude_titles=frozenset()):
        """Build a flat ``<ul>`` TOC from collected headings."""
        items = [
            (hid, title)
            for lvl, hid, title in self.toc_items
            if lvl >= 2 and title not in exclude_titles
        ]
        if not items:
            return ""
        parts = ["<ul>"]
        for hid, title in items:
            parts.append(f'  <li><a href="#{hid}">{title}</a></li>')
        parts.append("</ul>")
        return "\n".join(parts)


class _SmartDashInlineParser(InlineParser):
    """Inline parser that converts ``---`` to em-dash and ``--`` to en-dash.

    Because this is an inline rule, it naturally skips code spans, HTML tags,
    and other non-text tokens — no post-processing regex needed.
    """

    SPECIFICATION = dict(InlineParser.SPECIFICATION)
    # Match 2–3 consecutive dashes, but not 4+ (e.g. markdown horizontal rules).
    SPECIFICATION["smart_dash"] = r"-{2,3}(?!-)"

    DEFAULT_RULES = InlineParser.DEFAULT_RULES + ("smart_dash",)

    def parse_smart_dash(self, m, state):
        text = m.group(0)
        if text == "---":
            state.append_token({"type": "text", "raw": "\u2014"})
        else:
            state.append_token({"type": "text", "raw": "\u2013"})
        return m.end()


def _strip_kramdown_annotations(text):
    """Strip kramdown ``{:toc}`` / ``{:.no_toc}`` from Markdown source.

    Returns *(cleaned_text, no_toc_heading_names, has_toc)*.
    """
    no_toc = set()

    def _collect(m):
        heading_text = m.group(1).strip().lstrip("#").strip()
        no_toc.add(heading_text)
        return m.group(1)  # keep heading, drop annotation

    text = _NO_TOC_RE.sub(_collect, text)
    has_toc = bool(_TOC_RE.search(text))
    text = _TOC_RE.sub("<!-- TOC -->", text)
    return text, no_toc, has_toc


def _collapse_html_blanks(text):
    """Remove blank lines inside HTML block elements.

    Liquid templates produce blank lines from ``{% for %}``/``{% unless %}``
    tags.  Combined with indentation, these cause mistune to misparse the
    content as indented code blocks.  Removing blank lines while inside a
    block-level HTML element prevents this.
    """
    lines = text.split("\n")
    result = []
    depth = 0
    for line in lines:
        stripped = line.strip()
        depth += len(_BLOCK_OPEN_RE.findall(stripped)) - len(_BLOCK_CLOSE_RE.findall(stripped))
        if depth > 0 and not stripped:
            continue
        result.append(line)
    return "\n".join(result)


def _make_md(renderer):
    """Build a mistune Markdown instance with smart-dash inline parsing."""
    return Markdown(
        renderer=renderer,
        inline=_SmartDashInlineParser(),
        plugins=[table, footnotes, strikethrough],
    )


def render_markdown(text):
    """Convert Markdown to HTML (Pygments highlighting, TOC, smart dashes)."""
    # 1. Strip kramdown annotations that mistune doesn't understand.
    text, no_toc, has_toc = _strip_kramdown_annotations(text)

    # 2. Collapse inline HTML elements (<span>) that span multiple lines
    #    into single lines so mistune processes markdown inside them.
    text = _INLINE_SPAN_RE.sub(r"<\1>\2</span>", text)

    # 3. Remove blank lines inside HTML block elements.  Liquid templates
    #    produce blank lines from {% for %}/{% unless %} tags; combined
    #    with indentation, mistune would misparse them as code blocks.
    text = _collapse_html_blanks(text)

    # 4. Render Markdown → HTML.  A fresh renderer is used each time so
    #    that toc_items doesn't accumulate across calls.  Smart dashes
    #    are handled by _SmartDashInlineParser during parsing, so code
    #    spans and HTML are never touched.
    renderer = _JekyllRenderer()
    md = _make_md(renderer)
    out = md(text)

    # 5. Insert TOC at the placeholder, excluding {:.no_toc} headings.
    if has_toc:
        out = out.replace("<!-- TOC -->", renderer.render_toc(no_toc), 1)
    out = out.replace("<!-- TOC -->", "")
    return out


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
        # Resolve {% link %} tags once at load time.
        body = _resolve_link_tags(body, permalink_pattern)
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


def load_layouts(layouts_dir, permalink_pattern):
    """Load all layout templates, resolving {% link %} tags once."""
    layouts = {}
    if not os.path.isdir(layouts_dir):
        return layouts
    for fname in os.listdir(layouts_dir):
        path = os.path.join(layouts_dir, fname)
        if os.path.isfile(path):
            name = os.path.splitext(fname)[0]
            layouts[name] = _resolve_link_tags(read_file(path), permalink_pattern)
    return layouts


# ---------------------------------------------------------------------------
# Building
# ---------------------------------------------------------------------------


def build_site_data(site_cfg, posts, collections, pages):
    """Build the site-level variables dict for Liquid."""
    site = dict(site_cfg)
    site["posts"] = [dict(p["fm"]) for p in posts]
    site["pages"] = [dict(p["fm"]) for p in pages]
    for coll_name, coll_items in collections.items():
        site[coll_name] = [dict(p["fm"]) for p in coll_items]
    return site


def render_content(env, item, site, layouts, is_markdown):
    """Render a content item (post/page/collection item) to final HTML.

    Returns (final_html, rendered_body) where rendered_body is the content
    before layout wrapping (useful for post.content in templates).
    """
    fm = item["fm"]
    body = item["body"]

    page_vars = dict(fm)
    variables = {"site": site, "page": page_vars, "jekyll": {"environment": "development"}}

    # 1) Render Liquid in content body
    rendered_body = render_liquid(env, body, variables)

    # 2) Convert Markdown to HTML if needed
    if is_markdown:
        rendered_body = render_markdown(rendered_body)

    # 3) Apply layout
    layout_name = fm.get("layout")
    if layout_name and layout_name != "none" and layout_name in layouts:
        layout_src = layouts[layout_name]
        variables["content"] = rendered_body
        page_vars["content"] = rendered_body
        final = render_liquid(env, layout_src, variables)
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

    # Strip YAML frontmatter from .scss and rename to .css.
    # This site's SCSS is plain CSS so no preprocessing is needed.
    scss_path = os.path.join(dest_dir, "assets", "css", "main.scss")
    css_path = os.path.join(dest_dir, "assets", "css", "main.css")
    if os.path.isfile(scss_path):
        text = read_file(scss_path)
        _, body = parse_frontmatter(text)
        write_file(css_path, body)
        os.remove(scss_path)


def load_asset_pages(src_dir):
    """Find asset files that have YAML frontmatter and need Liquid processing."""
    pages = []
    skip_exts = {".scss"}  # Handled separately in copy_static
    for root, _dirs, files in os.walk(os.path.join(src_dir, "assets")):
        for fname in files:
            if os.path.splitext(fname)[1] in skip_exts:
                continue
            path = os.path.join(root, fname)
            try:
                text = read_file(path)
            except (UnicodeDecodeError, ValueError):
                continue
            if not text.startswith("---"):
                continue
            fm, body = parse_frontmatter(text)
            if not fm:
                fm = {}
            rel = os.path.relpath(path, src_dir)
            fm["url"] = "/" + rel
            fm["path"] = rel
            pages.append({"fm": fm, "body": body, "path": path, "ext": os.path.splitext(fname)[1]})
    return pages


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
    permalink = site_cfg.get("permalink", "/blog/:slug/")

    # Load layouts ({% link %} tags resolved once here)
    layouts = load_layouts(os.path.join(SRC, "_layouts"), permalink)

    # Load posts ({% link %} tags resolved once here)
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

    # Set up Liquid environment (includes have {% link %} resolved once)
    env = make_liquid_env(os.path.join(SRC, "_includes"), site_cfg)

    # --- Render posts (first pass: get content for post.content) ---
    print(f"Rendering {len(posts)} posts...")
    for i, p in enumerate(posts):
        output, body_html = render_content(env, p, site, layouts, is_markdown=True)
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
            output, body_html = render_content(env, item, site, layouts, is_markdown=True)
            out_path = os.path.join(DEST, output_path_for_url(item["fm"]["url"]))
            write_file(out_path, output)
            site[coll_name][i]["content"] = body_html

    # --- Render pages ---
    print(f"Rendering {len(pages)} pages...")
    for p in pages:
        is_md = p["ext"] == ".md"
        output, _ = render_content(env, p, site, layouts, is_markdown=is_md)
        out_path = os.path.join(DEST, output_path_for_url(p["fm"]["url"]))
        write_file(out_path, output)

    # --- Copy static files ---
    print("Copying static files...")
    copy_static(SRC, DEST)

    # --- Render asset files with frontmatter (e.g. rss.xsl) ---
    asset_pages = load_asset_pages(SRC)
    if asset_pages:
        print(f"Rendering {len(asset_pages)} asset pages...")
        for p in asset_pages:
            output, _ = render_content(env, p, site, layouts, is_markdown=False)
            out_path = os.path.join(DEST, p["fm"]["url"].lstrip("/"))
            write_file(out_path, output)

    # Count output files
    count = sum(len(files) for _, _, files in os.walk(DEST))
    print(f"Done! Generated {count} files in {DEST}")


if __name__ == "__main__":
    main()
