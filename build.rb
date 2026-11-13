#!/usr/bin/env ruby
# frozen_string_literal: true

# A small Jekyll-compatible static site builder, specialized for this site.
#
# Usage: bundle exec ruby build.rb [build] [options]
#
#   build          Build the site into _site (the default).
#   --future       Include posts dated in the future (overrides `future` in _config.yml).
#
# Markdown, Liquid, and syntax highlighting are the same libraries Jekyll uses
# (kramdown, liquid, rouge), configured the way Jekyll configures them, so the
# output should match `jekyll build`.
#
# The model:
#
# * A file is a *document* if it starts with YAML frontmatter (`---`).
#   Documents get Liquid rendering, then Markdown conversion (.md only), then
#   their layout. Everything else under STATIC is copied verbatim.
# * Each document's `content` is computed lazily and memoized, so pages like
#   feed.xml can read `post.content` regardless of render order.
# * A Site holds everything read in one build: documents, URLs, and layouts.

require "date"
require "fileutils"
require "json"
require "optparse"
require "time"
require "yaml"

require "kramdown"
require "kramdown-parser-gfm"
require "liquid"
require "rouge"

SRC = __dir__
DEST = File.join(SRC, "_site")

CONFIG = YAML.safe_load_file(File.join(SRC, "_config.yml"))

# Static files and directories copied to the output. Files in here that have
# frontmatter are rendered as pages instead (e.g. assets/rss.xsl).
STATIC = %w[assets demos resources .well-known favicon.ico CNAME dat.json].freeze

# Jekyll's kramdown defaults, plus the overrides in _config.yml.
KRAMDOWN = {
  input: "GFM",
  hard_wrap: false,
  auto_ids: true,
  toc_levels: (1..6).to_a,
  entity_output: "as_char",
  smart_quotes: "lsquo,rsquo,ldquo,rdquo",
  footnote_nr: 1,
  show_warnings: false,
  guess_lang: true,
  syntax_highlighter: "rouge",
  syntax_highlighter_opts: { default_lang: "plaintext", guess_lang: true },
}.freeze

# ---------------------------------------------------------------------------
# Liquid
# ---------------------------------------------------------------------------

TEMPLATES = {}

def template(source)
  TEMPLATES[source] ||= Liquid::Template.parse(source, error_mode: :strict)
end

# {% include css.md %} -- Jekyll-style, with an unquoted file name.
class IncludeTag < Liquid::Tag
  def initialize(tag_name, markup, parse_context)
    super
    @name = markup.strip
  end

  def render(context)
    source = File.read(File.join(SRC, "_includes", @name))
    template(source).render!(context)
  end
end

# {% link _posts/2020-09-05-compiling-a-lisp-4.md %} -- the URL of a document.
class LinkTag < Liquid::Tag
  def initialize(tag_name, markup, parse_context)
    super
    @path = markup.strip
  end

  def render(context)
    context.registers[:site].urls.fetch(@path) { raise "{% link %} to unknown document: #{@path}" }
  end
end

# {% seo %} -- a port of jekyll-seo-tag 2.8.0, minus the parts this site
# doesn't use (images, pagination, Facebook, webmaster verification, author
# hashes from _data). It omits the plugin's "Begin/End Jekyll SEO tag" comments
# and the `generator` meta tag, since this isn't Jekyll; everything else
# should be byte-identical.
class SeoTag < Liquid::Tag
  HOMEPAGE_OR_ABOUT = %r{^/(about/)?(index.html?)?$}

  def render(context)
    site = context["site"]
    page = context["page"]

    site_title = format_string(site["title"] || site["name"])
    site_description = format_string(site["description"])
    page_title = format_string(page["title"]) || site_title
    title = full_title(site, page_title, site_title, site_description)
    # The excerpt is looked up through the context so that Liquid evaluates
    # (and memoizes) the lazy Proc.
    description = format_string(page["description"] || context["page.excerpt"]) || site_description
    canonical_url = canonical_url(page)
    author_name = author(page, site)
    # jekyll-seo-tag falls back to the author's *name* for their Twitter handle.
    author_twitter = author_name&.delete_prefix("@")
    locale = page["locale"] || site["locale"] || page["lang"] || site["lang"] || "en_US"
    date = page["date"]

    tags = []
    tags << "<title>#{title}</title>" if title
    tags << %(<meta property="og:title" content="#{page_title}" />) if page_title
    tags << %(<meta name="author" content="#{author_name}" />) if author_name
    tags << %(<meta property="og:locale" content="#{locale.tr("-", "_")}" />)
    if description
      tags << %(<meta name="description" content="#{description}" />)
      tags << %(<meta property="og:description" content="#{description}" />)
    end
    if site["url"]
      tags << %(<link rel="canonical" href="#{canonical_url}" />)
      tags << %(<meta property="og:url" content="#{canonical_url}" />)
    end
    tags << %(<meta property="og:site_name" content="#{site_title}" />) if site_title
    if date
      tags << %(<meta property="og:type" content="article" />)
      tags << %(<meta property="article:published_time" content="#{date.xmlschema}" />)
    else
      tags << %(<meta property="og:type" content="website" />)
    end
    tags << %(<meta name="twitter:card" content="summary" />)
    tags << %(<meta property="twitter:title" content="#{page_title}" />) if page_title
    if site["twitter"]
      tags << %(<meta name="twitter:site" content="@#{site["twitter"]["username"].to_s.delete("@")}" />)
      tags << %(<meta name="twitter:creator" content="@#{author_twitter}" />) if author_twitter
    end

    json_ld = json_ld(
      page: page,
      site: site,
      site_title: site_title,
      page_title: page_title,
      description: description,
      canonical_url: canonical_url,
      author_name: author_name,
    )
    tags << %(<script type="application/ld+json">\n#{json_ld}</script>)

    tags.map { |tag| "#{tag}\n" }.join
  end

  private

  # Titles and descriptions go through Markdown and come out as escaped plain
  # text, so e.g. `'` becomes a curly quote and `code` loses its backticks.
  def format_string(text)
    text = FILTERS.markdownify(text)
    text = FILTERS.strip_html(text)
    text = FILTERS.normalize_whitespace(text)
    text = FILTERS.escape_once(text)
    text unless text.empty?
  end

  def full_title(site, page_title, site_title, site_description)
    if site_title && page_title != site_title
      "#{page_title} | #{site_title}"
    elsif site_description && site_title
      "#{site_title} | #{format_string(site["tagline"]) || site_description}"
    else
      page_title || site_title
    end
  end

  def canonical_url(page)
    if page["canonical_url"].to_s.empty?
      FILTERS.absolute_url(page["url"]).sub(%r{/index\.html$}, "/")
    else
      page["canonical_url"]
    end
  end

  def author(page, site)
    [page["author"], site["author"]].find { |author| !author.to_s.empty? }
  end

  def json_ld(page:, site:, site_title:, page_title:, description:, canonical_url:, author_name:)
    homepage = page["url"].match?(HOMEPAGE_OR_ABOUT)
    date = page["date"]
    modified = page["last_modified_at"] || date
    type = if homepage
      "WebSite"
    elsif date
      "BlogPosting"
    else
      "WebPage"
    end

    data = {
      "@context" => "https://schema.org",
      "@type" => type,
      "author" => author_name && { "@type" => "Person", "name" => author_name },
      "dateModified" => modified && parse_date(modified).xmlschema,
      "datePublished" => date&.xmlschema,
      "description" => description,
      "headline" => page_title,
      "mainEntityOfPage" => type == "BlogPosting" ? { "@type" => "WebPage", "@id" => canonical_url } : nil,
      "name" => homepage ? site_title : nil,
      "url" => canonical_url,
    }
    data.compact.sort.to_h.to_json
  end
end

# The Jekyll filters this site uses.
module JekyllFilters
  def xml_escape(input)
    input.to_s.encode(xml: :attr).delete_prefix('"').delete_suffix('"')
  end

  def absolute_url(input)
    input = input.to_s
    return input if input.match?(%r{\Ahttps?://})

    CONFIG["url"] + input
  end

  def markdownify(input)
    Kramdown::Document.new(input.to_s, KRAMDOWN).to_html
  end

  def normalize_whitespace(input)
    input.to_s.gsub(/\s+/, " ").strip
  end

  # {{ items | where_exp: "item", "item.index != true" }}
  def where_exp(input, variable, expression)
    condition = template("{% if #{expression} %}true{% endif %}")
    input.select { |item| condition.render!(variable => item) == "true" }
  end
end

# For calling filters from Ruby.
FILTERS = Object.new.extend(Liquid::StandardFilters, JekyllFilters)

Liquid::Template.register_tag("include", IncludeTag)
Liquid::Template.register_tag("link", LinkTag)
Liquid::Template.register_tag("seo", SeoTag)
Liquid::Template.register_filter(JekyllFilters)

# ---------------------------------------------------------------------------
# Documents
# ---------------------------------------------------------------------------

class Document
  attr_reader :path, :data

  def initialize(site, path, data, body)
    @site = site
    @path = path
    @data = data
    @body = body
  end

  def url
    data["url"]
  end

  def markdown?
    path.end_with?(".md")
  end

  # The rendered body, before the layout is applied.
  def content
    @content ||= convert(@site.render_liquid(@body, data))
  end

  # Jekyll's excerpt: everything up to the first excerpt separator (by default
  # the first paragraph), rendered like the body. jekyll-seo-tag uses it as the
  # description for pages that don't have one.
  def excerpt
    @excerpt ||= convert(@site.render_liquid(excerpt_source, data.except("layout", "excerpt")))
  end

  # The full output, with the layout applied.
  def output
    name = data["layout"]
    return content if name.nil? || name == "none"

    layout = @site.layouts.fetch(name) { raise "#{path}: unknown layout #{name.inspect}" }
    @site.render_liquid(layout, data, "content" => content)
  end

  private

  def convert(html)
    markdown? ? Kramdown::Document.new(html, KRAMDOWN).to_html : html
  end

  LIQUID_TAG = /{%-?\s*(\w+)\s*.*?-?%}/m
  LINK_REFERENCE_DEFINITION = /^ {0,3}(?:(\[[^\]]+\])(:.+))$/

  def excerpt_separator
    data["excerpt_separator"] || CONFIG["excerpt_separator"] || "\n\n"
  end

  # Ported from Jekyll::Excerpt#extract_excerpt.
  def excerpt_source
    head, _, tail = @body.partition(excerpt_separator)
    return head if tail.empty?

    head = close_liquid_blocks(head) if head.include?("{%")

    # Keep [link][references] working by copying over their definitions.
    definitions = tail.scan(LINK_REFERENCE_DEFINITION).filter_map do |label, rest|
      "#{label}#{rest}" if head.include?(label)
    end
    return head if definitions.empty?

    "#{head}\n\n#{definitions.join("\n")}"
  end

  # If the excerpt cuts a Liquid block (e.g. {% raw %}) in half, close it.
  def close_liquid_blocks(head)
    head.scan(LIQUID_TAG).flatten.reverse_each do |tag_name|
      tag = Liquid::Template.tags[tag_name]
      next unless tag && tag < Liquid::Block
      next if head.match?(/{%-?\s*end#{tag_name}.*?\s*-?%}/m)

      head += "\n{% end#{tag_name} %}"
    end
    head
  end

  public

  # Where this document is written, relative to the output directory.
  def output_path
    if url.end_with?("/")
      File.join(url, "index.html")
    elsif File.extname(url).empty?
      "#{url}.html"
    else
      url
    end
  end
end

# Jekyll's frontmatter regex, verbatim. Note that `\s*$` also eats blank
# lines after the closing `---`.
FRONTMATTER = /\A(---\s*\n.*?\n?)^((---|\.\.\.)\s*$\n?)/m

def frontmatter?(full_path)
  File.open(full_path, "rb") { |f| f.read(3) } == "---"
end

def parse_date(value)
  # Same as Jekyll: stringify, parse, convert to local time.
  Time.parse(value.to_s).localtime
end

def basename(path)
  File.basename(path, ".*")
end

# Jekyll's URLs for pages: dir/foo.md and dir/foo.html become /dir/foo/,
# dir/index.md becomes /dir/, .scss becomes .css, and everything else keeps
# its path (e.g. /feed.xml, /assets/rss.xsl).
def page_url(path)
  dir = File.dirname(path)
  dir = "" if dir == "."
  name = basename(path)

  case File.extname(path)
  when ".md", ".html"
    name == "index" ? File.join("/", dir, "") : File.join("/", dir, name, "")
  when ".scss"
    File.join("/", dir, "#{name}.css")
  else
    "/#{path}"
  end
end

# Jekyll's sort order for collection documents: by date, then by path.
def sort_by_date(documents)
  documents.sort_by { |document| [document.data["date"], document.path] }
end

# ---------------------------------------------------------------------------
# Site
# ---------------------------------------------------------------------------

class Site
  attr_reader :time, :posts, :blog_lisp, :recipes, :pages, :static_files,
              :documents, :urls, :layouts

  def initialize(future: false)
    @time = Time.now

    @posts = read_documents("_posts/*.md", collection: true) do |path, data|
      slug = data["slug"] || basename(path).sub(/\A\d{4}-\d\d-\d\d-/, "")
      CONFIG["permalink"].sub(":slug", slug)
    end
    @posts = @posts.select { |post| future || post.data["date"] <= @time }
    @posts = sort_by_date(@posts).reverse

    @blog_lisp = read_documents("_blog_lisp/*.md", collection: true) { |path| "/blog/lisp/#{basename(path)}/" }
    @blog_lisp = sort_by_date(@blog_lisp)
    @recipes = read_documents("_recipes/*.md", collection: true) { |path| "/recipes/#{basename(path)}/" }
    @recipes = sort_by_date(@recipes)

    # Dotfiles are skipped, except for directories named explicitly in STATIC.
    @static_files = Dir.glob("{#{STATIC.join(",")}}{,/**/*}", base: SRC)
    @static_files = @static_files.select { |path| File.file?(File.join(SRC, path)) }.sort

    root_pages = read_documents("*.{md,html,xml}") { |path| page_url(path) }
    static_pages = @static_files.filter_map { |path| read_document(path) { page_url(path) } }
    @pages = (root_pages + static_pages).sort_by { |page| [File.basename(page.path), page.path] }

    @documents = @posts + @blog_lisp + @recipes + @pages
    @urls = @documents.to_h { |document| [document.path, document.url] }

    @layouts = Dir.glob(File.join(SRC, "_layouts", "*")).to_h do |path|
      [basename(path), File.read(path)]
    end

    # `site` in Liquid.
    @liquid = CONFIG.merge(
      "time" => @time,
      "posts" => @posts.map(&:data),
      "pages" => @pages.map(&:data),
      "blog_lisp" => @blog_lisp.map(&:data),
      "recipes" => @recipes.map(&:data),
    )
  end

  def render_liquid(source, page, extra_vars = {})
    vars = {
      "site" => @liquid,
      "page" => page,
      "jekyll" => { "environment" => ENV.fetch("JEKYLL_ENV", "development") },
    }
    template(source).render!(vars.merge(extra_vars), registers: { site: self }, strict_filters: true)
  end

  # Write the site to `dest`. The site is built in a temporary directory and
  # swapped in at the end, so a failed build never leaves a half-written site
  # behind, and there is always a complete site at `dest`.
  def write(dest)
    tmp = "#{dest}.tmp"
    old = "#{dest}.old"
    FileUtils.rm_rf([tmp, old])

    copied = static_files.reject { |path| urls.key?(path) }
    copied.each do |path|
      write_file(tmp, path) { |file| FileUtils.cp(File.join(SRC, path), file) }
    end

    documents.each do |document|
      write_file(tmp, document.output_path) { |file| File.write(file, document.output) }
    end

    # Like jekyll-sitemap, don't overwrite a sitemap.xml or robots.txt from the source.
    generated = {
      "sitemap.xml" => -> { sitemap_xml(copied) },
      "robots.txt" => -> { robots_txt },
    }
    generated.each do |path, contents|
      next if File.exist?(File.join(tmp, path))

      write_file(tmp, path) { |file| File.write(file, contents.call) }
    end

    File.rename(dest, old) if File.exist?(dest)
    File.rename(tmp, dest)
    FileUtils.rm_rf(old)
  rescue StandardError
    FileUtils.rm_rf(tmp)
    raise
  else
    puts "Rendered #{documents.size} documents and copied #{copied.size} static files to #{dest}"
  end

  private

  # Read the document at `path` (relative to SRC), or return nil if it has no
  # frontmatter. The block computes the document's URL from its path and data;
  # a `permalink` in the frontmatter takes precedence.
  #
  # Like Jekyll, collection documents (posts, blog_lisp, recipes) always have a
  # date (the build time if nothing else) and an excerpt; pages have neither
  # unless they set them in their frontmatter.
  def read_document(path, collection: false)
    full_path = File.join(SRC, path)
    return nil unless frontmatter?(full_path)

    match = File.read(full_path).match(FRONTMATTER)
    return nil unless match

    data = YAML.safe_load(match[1], permitted_classes: [Date, Time]) || {}
    data["path"] = path
    date = data["date"] || path[/\A_posts\/(\d{4}-\d\d-\d\d)-/, 1]
    data["date"] = parse_date(date) if date
    data["date"] ||= @time if collection
    data["url"] = data["permalink"] || yield(path, data)

    document = Document.new(self, path, data, match.post_match)
    # Liquid calls Procs found in hashes and memoizes the result, so
    # `post.content` is rendered on first use.
    data["content"] = -> { document.content }
    data["excerpt"] ||= -> { document.excerpt } if collection
    document
  end

  def read_documents(glob, collection: false, &url)
    Dir.glob(glob, base: SRC).sort.filter_map do |path|
      read_document(path, collection: collection, &url)
    end
  end

  def write_file(root, relative_path)
    file = File.join(root, relative_path)
    FileUtils.mkdir_p(File.dirname(file))
    yield file
  end

  # sitemap.xml and robots.txt -- a port of jekyll-sitemap 1.4.0.

  SITEMAP_STATIC_EXTENSIONS = %w[.htm .html .xhtml .pdf].freeze

  def sitemap_entry(url, lastmod)
    loc = FILTERS.xml_escape(FILTERS.absolute_url(url.gsub("/index.html", "/")))
    lines = ["<url>", "<loc>#{loc}</loc>"]
    lines << "<lastmod>#{lastmod.xmlschema}</lastmod>" if lastmod
    lines << "</url>"
    lines
  end

  # Collection documents (collections in alphabetical order, documents oldest
  # first), then HTML pages, then static HTML and PDF files.
  def sitemap_xml(copied_static_files)
    lines = [
      %(<?xml version="1.0" encoding="UTF-8"?>),
      %(<urlset xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" ) +
        %(xsi:schemaLocation="http://www.sitemaps.org/schemas/sitemap/0.9 ) +
        %(http://www.sitemaps.org/schemas/sitemap/0.9/sitemap.xsd" ) +
        %(xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">),
    ]

    collections = { "posts" => posts.reverse, "blog_lisp" => blog_lisp, "recipes" => recipes }
    collections.sort.each do |_name, documents|
      documents.each do |document|
        next if document.data["sitemap"] == false

        lastmod = parse_date(document.data["last_modified_at"] || document.data["date"])
        lines.concat(sitemap_entry(document.url, lastmod))
      end
    end

    pages.each do |page|
      next if page.data["sitemap"] == false
      next if page.url == "/404.html"
      next unless page.output_path.end_with?(".html")

      lastmod = page.data["last_modified_at"]
      lines.concat(sitemap_entry(page.url, lastmod && parse_date(lastmod)))
    end

    copied_static_files.each do |path|
      next unless SITEMAP_STATIC_EXTENSIONS.include?(File.extname(path))
      next if File.basename(path) == "404.html"

      lines.concat(sitemap_entry("/#{path}", File.mtime(File.join(SRC, path))))
    end

    lines << "</urlset>"
    lines.map { |line| "#{line}\n" }.join
  end

  def robots_txt
    "Sitemap: #{FILTERS.absolute_url("/sitemap.xml")}\n"
  end
end

def build(future:)
  Site.new(future: future).write(DEST)
end

# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------

options = { future: CONFIG["future"] }
OptionParser.new do |parser|
  parser.banner = "Usage: bundle exec ruby build.rb [build] [options]"
  parser.on("--future", "Include posts dated in the future") { options[:future] = true }
end.parse!

case ARGV.fetch(0, "build")
when "build"
  build(future: options[:future])
else
  abort "Unknown command #{ARGV[0].inspect}; expected build"
end
