#!/usr/bin/env ruby
# frozen_string_literal: true

# minijekyll.rb -- a single-file reimplementation of the subset of Jekyll that
# bernsteinbear.com uses.
#
# Hard dependencies: kramdown, kramdown-parser-gfm, rouge, liquid (4.x).
#   Declared in Gemfile.minijekyll and vendored as .gem files in vendor/cache:
#     BUNDLE_GEMFILE=Gemfile.minijekyll bundle install --local
#     BUNDLE_GEMFILE=Gemfile.minijekyll bundle exec ruby minijekyll.rb
# Optional: sass-embedded (or a `sass` binary on PATH) to compile .scss pages,
#           addressable for Jekyll-identical URL normalization (a stdlib
#           fallback gives the same output for this site).
#
# What it replicates:
#   * _config.yml (url, permalink, collections, include/exclude, future,
#     kramdown + rouge settings), exposed verbatim as `site.*`
#   * YAML front matter, posts (_posts), collections (_blog_lisp, _recipes),
#     pages (any file with front matter), static-file copying, layouts chain
#   * Liquid rendering before Markdown, Jekyll's `include` tag and the
#     `absolute_url`, `xml_escape`, `where`, `where_exp`, `date_to_xmlschema`,
#     `jsonify`, `markdownify`, `strip_html`, `normalize_whitespace` filters
#   * kramdown with Jekyll's default option set (GFM, footnotes, toc, rouge)
#   * the `{% seo %}` tag (jekyll-seo-tag 2.8.0 behaviour, template embedded)
#   * sitemap.xml and robots.txt (jekyll-sitemap 1.4.0 behaviour)
#   * post excerpts (used by the seo tag for meta descriptions)
#
# Usage: ruby minijekyll.rb [--source DIR] [--dest DIR] [--future] [--env ENV]

require "yaml"
require "time"
require "date"
require "json"
require "cgi"
require "fileutils"
require "tmpdir"
require "optparse"
require "set"
require "kramdown"
require "kramdown-parser-gfm"
require "rouge"
require "liquid"
require "uri"
begin
  require "addressable/uri"
rescue LoadError
  # optional; see Utils.normalize_uri
end

module MiniJekyll
  JEKYLL_VERSION = "4.4.0"   # reported in <meta name="generator">
  SEO_TAG_VERSION = "2.8.0"  # reported in the seo tag's HTML comment

  FRONT_MATTER = /\A(---\s*\n.*?\n?)^((---|\.\.\.)\s*$\n?)/m
  DATE_FILENAME = /\A(\d{2,4}-\d{1,2}-\d{1,2})-(.*)\z/
  MARKDOWN_EXTS = %w[.markdown .mkdown .mkdn .mkd .md].freeze
  SASS_EXTS = %w[.scss .sass].freeze
  DEFAULT_EXCLUDES = %w[
    .sass-cache .jekyll-cache gemfiles Gemfile Gemfile.lock node_modules
    vendor/bundle/ vendor/cache/ vendor/gems/ vendor/ruby/
  ].freeze
  KRAMDOWN_DEFAULTS = {
    "auto_ids" => true,
    "toc_levels" => [1, 2, 3, 4, 5, 6],
    "entity_output" => "as_char",
    "smart_quotes" => "lsquo,rsquo,ldquo,rdquo",
    "input" => "GFM",
    "hard_wrap" => false,
    "guess_lang" => true,
    "footnote_nr" => 1,
    "show_warnings" => false,
  }.freeze
  POST_PERMALINK_STYLES = {
    "pretty" => "/:categories/:year/:month/:day/:title/",
    "date" => "/:categories/:year/:month/:day/:title:output_ext",
    "ordinal" => "/:categories/:year/:y_day/:title:output_ext",
    "weekdate" => "/:categories/:year/W:week/:short_day/:title:output_ext",
    "none" => "/:categories/:title:output_ext",
  }.freeze

  # ---------------------------------------------------------------------------
  # Small helpers
  # ---------------------------------------------------------------------------
  module Utils
    module_function

    # Jekyll::Utils.parse_date
    def parse_date(input)
      Time.parse(input.to_s).localtime
    rescue ArgumentError
      raise "Could not parse date #{input.inspect}"
    end

    # Jekyll::Utils.slugify, mode "default"
    def slugify(str)
      str.to_s.gsub(/[^\p{M}\p{L}\p{Nd}]+/, "-").gsub(/\A-|-\z/, "").downcase
    end

    def titleize_slug(slug)
      slug.split("-").map(&:capitalize).join(" ")
    end

    def has_front_matter?(path)
      File.open(path, "rb", &:readline).match?(/\A---\s*\r?\n/)
    rescue EOFError
      false
    end

    # Returns [data_or_nil, body]
    def split_front_matter(text)
      if (m = FRONT_MATTER.match(text))
        data = YAML.safe_load(m[1], permitted_classes: [Date, Time], aliases: true) || {}
        [data, m.post_match]
      else
        [nil, text]
      end
    end

    def read(path)
      File.read(path, encoding: "utf-8")
    end

    # Jekyll::URL#sanitize_url
    def sanitize_url(url)
      url.gsub(%r{/{2,}}, "/").gsub(%r{\A([^/]|\z)}, '/\1').gsub(%r{(?<=/)\./}, "")
    end

    # Jekyll::URL.generate_url
    def generate_url(template, placeholders)
      sanitize_url(template.gsub(/:([a-z_]+)/) { placeholders.fetch(Regexp.last_match(1)).to_s })
    end

    # Jekyll's url filters pass their result through Addressable::URI#normalize,
    # which percent-encodes characters such as spaces. Use addressable when it
    # is installed and fall back to the stdlib escaper otherwise.
    def normalize_uri(str)
      if defined?(::Addressable::URI)
        ::Addressable::URI.parse(str).normalize.to_s
      else
        # Unescape first so that normalizing an already-normalized URL (as
        # absolute_url does with relative_url's result) is idempotent.
        parser = ::URI::DEFAULT_PARSER
        parser.escape(parser.unescape(str))
      end
    rescue StandardError
      str
    end

    def absolute_uri?(str)
      str.to_s.match?(%r{\A[a-z][a-z0-9+.-]*:}i)
    end
  end

  # ---------------------------------------------------------------------------
  # Markdown (kramdown configured the way Jekyll configures it)
  # ---------------------------------------------------------------------------
  class Markdown
    def initialize(config)
      opts = KRAMDOWN_DEFAULTS.merge(config["kramdown"] || {})
      opts["syntax_highlighter"] ||= config["highlighter"] || "rouge"
      opts["syntax_highlighter_opts"] ||= {}
      opts["syntax_highlighter_opts"]["default_lang"] ||= "plaintext"
      opts["syntax_highlighter_opts"]["guess_lang"] = opts["guess_lang"]
      @options = symbolize(opts)
    end

    def convert(text)
      Kramdown::Document.new(text, @options).to_html
    end

    private

    def symbolize(obj)
      case obj
      when Hash then obj.each_with_object({}) { |(k, v), h| h[k.to_sym] = symbolize(v) }
      when Array then obj.map { |v| symbolize(v) }
      else obj
      end
    end
  end

  # ---------------------------------------------------------------------------
  # Liquid filters (Jekyll::Filters subset + seo-tag/sitemap needs)
  # ---------------------------------------------------------------------------
  module Filters
    HTML_ESCAPE = { "&" => "&amp;", ">" => "&gt;", "<" => "&lt;", '"' => "&quot;", "'" => "&#39;" }.freeze
    HTML_ESCAPE_ONCE_REGEXP = /["><']|&(?!([a-zA-Z]+|(#\d+));)/.freeze
    STRIP_HTML_BLOCK_REGEX = %r{(<script.*?</script>)|(<!--.*?-->)|(<style.*?</style>)}m.freeze
    STRIP_HTML_TAG_REGEX = /<.*?>/m.freeze

    def site
      @context.registers[:site]
    end

    def markdownify(input)
      site.markdown.convert(input.to_s)
    end

    def strip_html(input)
      input.to_s.gsub(STRIP_HTML_BLOCK_REGEX, "").gsub(STRIP_HTML_TAG_REGEX, "")
    end

    def normalize_whitespace(input)
      input.to_s.gsub(/\s+/, " ").strip
    end

    def escape_once(input)
      input.to_s.gsub(HTML_ESCAPE_ONCE_REGEXP, HTML_ESCAPE)
    end

    def xml_escape(input)
      input.to_s.encode(xml: :attr).gsub(/\A"|"\z/, "")
    end

    def jsonify(input)
      input.to_json
    end

    def relative_url(input)
      return if input.nil?
      input = input["url"] if input.is_a?(Hash) && input.key?("url")
      return input if Utils.absolute_uri?(input)
      base = site.config["baseurl"].to_s.chomp("/")
      Utils.normalize_uri([base, input.to_s].map { |p| p.empty? || p.start_with?("/") ? p : "/#{p}" }.join)
    end

    def absolute_url(input)
      return if input.nil?
      input = input["url"] if input.is_a?(Hash) && input.key?("url")
      return input if Utils.absolute_uri?(input)
      site_url = site.config["url"].to_s
      return relative_url(input) if site_url.empty?
      Utils.normalize_uri(site_url + relative_url(input))
    end

    def date_to_xmlschema(input)
      t = to_time(input)
      t&.xmlschema
    end

    def date_to_rfc822(input)
      to_time(input)&.rfc822
    end

    def date_to_string(input)
      to_time(input)&.strftime("%d %b %Y")
    end

    # Jekyll's `where` compares stringified values.
    def where(input, property, value)
      return input if !property || value.is_a?(Array) || value.is_a?(Hash)
      return input unless input.respond_to?(:select)
      input = input.values if input.is_a?(Hash)
      input.select do |object|
        item = item_property(object, property)
        if item.is_a?(Array)
          item.any? { |v| v.to_s == value.to_s }
        else
          item.to_s == value.to_s
        end
      end || []
    end

    def where_exp(input, variable, expression)
      return input unless input.respond_to?(:select)
      input = input.values if input.is_a?(Hash)
      condition = parse_condition(expression)
      @context.stack do
        input.select do |object|
          @context[variable] = object
          condition.evaluate(@context)
        end
      end || []
    end

    private

    def to_time(input)
      case input
      when Time then input.clone.localtime
      when Date then input.to_time.localtime
      when String
        return nil if input.empty?
        Time.parse(input).localtime
      when Numeric then Time.at(input).localtime
      end
    end

    def item_property(item, property)
      if item.respond_to?(:to_liquid)
        liq = item.to_liquid
        liq.respond_to?(:[]) ? liq[property.to_s] : nil
      elsif item.respond_to?(:[])
        item[property.to_s]
      end
    end

    def parse_condition(exp)
      parser = Liquid::Parser.new(exp)
      condition = parse_binary_comparison(parser)
      parser.consume(:end_of_string)
      condition
    end

    def parse_binary_comparison(parser)
      condition = parse_comparison(parser)
      first_condition = condition
      while (binary_operator = parser.id?("and") || parser.id?("or"))
        child_condition = parse_comparison(parser)
        condition.send(binary_operator, child_condition)
        condition = child_condition
      end
      first_condition
    end

    def parse_comparison(parser)
      left_operand = Liquid::Expression.parse(parser.expression)
      operator = parser.consume?(:comparison)
      return Liquid::Condition.new(left_operand) unless operator
      right_operand = Liquid::Expression.parse(parser.expression)
      Liquid::Condition.new(left_operand, operator, right_operand)
    end
  end

  # ---------------------------------------------------------------------------
  # {% include file.ext key="value" %}
  # ---------------------------------------------------------------------------
  class IncludeTag < Liquid::Tag
    PARAM_SYNTAX = /([\w-]+)\s*=\s*(?:"([^"\\]*(?:\\.[^"\\]*)*)"|'([^'\\]*(?:\\.[^'\\]*)*)'|([\w.-]+))/.freeze

    def initialize(tag_name, markup, parse_context)
      super
      markup = markup.strip
      raise Liquid::SyntaxError, "include tag needs a file name" if markup.empty?
      @file, @params = markup.split(/\s+/, 2)
    end

    def render(context)
      site = context.registers[:site]
      file = @file.include?("{{") ? Liquid::Template.parse(@file).render(context) : @file
      path = File.join(site.source, "_includes", file)
      raise Liquid::Error, "Included file '_includes/#{file}' not found" unless File.file?(path)

      template = site.include_template(path)
      context.stack do
        context["include"] = parse_params(context)
        template.render!(context)
      end
    end

    private

    def parse_params(context)
      params = {}
      return params unless @params
      @params.scan(PARAM_SYNTAX) do |key, d_quoted, s_quoted, variable|
        params[key] =
          if d_quoted then d_quoted.include?('\\"') ? d_quoted.gsub('\\"', '"') : d_quoted
          elsif s_quoted then s_quoted.include?("\\'") ? s_quoted.gsub("\\'", "'") : s_quoted
          elsif variable then context[variable]
          end
      end
      params
    end
  end

  # ---------------------------------------------------------------------------
  # {% link _posts/2020-01-01-foo.md %} -> that item's URL
  # ---------------------------------------------------------------------------
  class LinkTag < Liquid::Tag
    def initialize(tag_name, markup, parse_context)
      super
      @relative_path = markup.strip
    end

    def render(context)
      site = context.registers[:site]
      rel = Liquid::Template.parse(@relative_path).render(context)
      item = site.find_item(rel) || site.find_item(rel.sub(%r{\A/}, ""))
      raise ArgumentError, "Could not find document '#{rel}' in tag 'link'" unless item
      base = site.config["baseurl"].to_s.chomp("/")
      base + item.url
    end
  end

  # ---------------------------------------------------------------------------
  # {% seo %} -- jekyll-seo-tag 2.8.0 (MIT, Ben Balter). Template embedded.
  # ---------------------------------------------------------------------------
  class SeoTag < Liquid::Tag
    MINIFY_REGEX = /(>\n|[%}]})\s+(<|{[{%])/.freeze
    TEMPLATE = <<~'HTML'
      <!-- Begin Jekyll SEO tag v{{ seo_tag.version }} -->
      {% if seo_tag.title? %}
        <title>{{ seo_tag.title }}</title>
      {% endif %}

      <meta name="generator" content="Jekyll v{{ jekyll.version }}" />

      {% if seo_tag.page_title %}
        <meta property="og:title" content="{{ seo_tag.page_title }}" />
      {% endif %}

      {% if seo_tag.author.name %}
        <meta name="author" content="{{ seo_tag.author.name }}" />
      {% endif %}

      <meta property="og:locale" content="{{ seo_tag.page_locale }}" />

      {% if seo_tag.description %}
        <meta name="description" content="{{ seo_tag.description }}" />
        <meta property="og:description" content="{{ seo_tag.description }}" />
      {% endif %}

      {% if site.url %}
        <link rel="canonical" href="{{ seo_tag.canonical_url }}" />
        <meta property="og:url" content="{{ seo_tag.canonical_url }}" />
      {% endif %}

      {% if seo_tag.site_title %}
        <meta property="og:site_name" content="{{ seo_tag.site_title }}" />
      {% endif %}

      {% if seo_tag.image %}
        <meta property="og:image" content="{{ seo_tag.image.path }}" />
        {% if seo_tag.image.height %}
          <meta property="og:image:height" content="{{ seo_tag.image.height }}" />
        {% endif %}
        {% if seo_tag.image.width %}
          <meta property="og:image:width" content="{{ seo_tag.image.width }}" />
        {% endif %}
        {% if seo_tag.image.alt %}
          <meta property="og:image:alt" content="{{ seo_tag.image.alt }}" />
        {% endif %}
      {% endif %}

      {% if page.date %}
        <meta property="og:type" content="article" />
        <meta property="article:published_time" content="{{ page.date | date_to_xmlschema }}" />
      {% else %}
        <meta property="og:type" content="website" />
      {% endif %}

      {% if paginator.previous_page %}
        <link rel="prev" href="{{ paginator.previous_page_path | absolute_url }}" />
      {% endif %}
      {% if paginator.next_page %}
        <link rel="next" href="{{ paginator.next_page_path | absolute_url }}" />
      {% endif %}

      {% if seo_tag.image %}
        <meta name="twitter:card" content="{{ page.twitter.card | default: site.twitter.card | default: "summary_large_image" }}" />
        <meta property="twitter:image" content="{{ seo_tag.image.path }}" />
      {% else %}
        <meta name="twitter:card" content="summary" />
      {% endif %}

      {% if seo_tag.image.alt %}
        <meta name="twitter:image:alt" content="{{ seo_tag.image.alt }}" />
      {% endif %}

      {% if seo_tag.page_title %}
        <meta property="twitter:title" content="{{ seo_tag.page_title }}" />
      {% endif %}

      {% if site.twitter %}
        <meta name="twitter:site" content="@{{ site.twitter.username | remove:'@' }}" />

        {% if seo_tag.author.twitter %}
          <meta name="twitter:creator" content="@{{ seo_tag.author.twitter | remove:'@' }}" />
        {% endif %}
      {% endif %}

      {% if site.facebook %}
        {% if site.facebook.admins %}
          <meta property="fb:admins" content="{{ site.facebook.admins }}" />
        {% endif %}

        {% if site.facebook.publisher %}
          <meta property="article:publisher" content="{{ site.facebook.publisher }}" />
        {% endif %}

        {% if site.facebook.app_id %}
          <meta property="fb:app_id" content="{{ site.facebook.app_id }}" />
        {% endif %}
      {% endif %}

      {% if site.webmaster_verifications %}
        {% if site.webmaster_verifications.google %}
          <meta name="google-site-verification" content="{{ site.webmaster_verifications.google }}" />
        {% endif %}

        {% if site.webmaster_verifications.bing %}
          <meta name="msvalidate.01" content="{{ site.webmaster_verifications.bing }}" />
        {% endif %}

        {% if site.webmaster_verifications.alexa %}
          <meta name="alexaVerifyID" content="{{ site.webmaster_verifications.alexa }}" />
        {% endif %}

        {% if site.webmaster_verifications.yandex %}
          <meta name="yandex-verification" content="{{ site.webmaster_verifications.yandex }}" />
        {% endif %}

        {% if site.webmaster_verifications.baidu %}
          <meta name="baidu-site-verification" content="{{ site.webmaster_verifications.baidu }}" />
        {% endif %}

        {% if site.webmaster_verifications.facebook %}
          <meta name="facebook-domain-verification" content="{{ site.webmaster_verifications.facebook }}" />
        {% endif %}
      {% elsif site.google_site_verification %}
        <meta name="google-site-verification" content="{{ site.google_site_verification }}" />
      {% endif %}

      <script type="application/ld+json">
        {{ seo_tag.json_ld | jsonify }}
      </script>

      <!-- End Jekyll SEO tag -->
    HTML

    def self.template
      @template ||= Liquid::Template.parse(TEMPLATE.gsub(MINIFY_REGEX, '\1\2'))
    end

    def initialize(tag_name, text, parse_context)
      super
      @text = text
    end

    def render(context)
      page = context["page"]
      item = context.registers[:page]
      site_hash = context["site"]
      drop = SeoData.new(context, @text, page, site_hash, item)
      payload = {
        "page" => page,
        "site" => site_hash,
        "paginator" => context["paginator"],
        "seo_tag" => drop.to_h,
        "jekyll" => { "version" => JEKYLL_VERSION },
      }
      self.class.template.render!(payload, registers: context.registers)
    end
  end

  # Computes the `seo_tag` drop as a plain hash. Mirrors Jekyll::SeoTag::Drop.
  class SeoData
    TITLE_SEPARATOR = " | "
    HOMEPAGE_OR_ABOUT_REGEX = %r{^/(about/)?(index.html?)?$}.freeze

    def initialize(context, text, page, site, item)
      @context = context
      @text = text
      @page = page || {}
      @site = site || {}
      @item = item
      @filters = Object.new.extend(Filters)
      @filters.instance_variable_set(:@context, context)
    end

    def to_h
      {
        "version" => SEO_TAG_VERSION,
        "title?" => title?,
        "title" => title,
        "page_title" => page_title,
        "site_title" => site_title,
        "description" => description,
        "author" => author,
        "page_locale" => page_locale,
        "canonical_url" => canonical_url,
        "image" => image,
        "json_ld" => json_ld,
      }
    end

    def title?
      return false unless title
      @text !~ /title=false/i
    end

    def site_title
      @site_title ||= format_string(@site["title"] || @site["name"])
    end

    def site_description
      @site_description ||= format_string(@site["description"])
    end

    def site_tagline
      @site_tagline ||= format_string(@site["tagline"])
    end

    def page_title
      @page_title ||= format_string(@page["title"]) || site_title
    end

    def title
      @title ||=
        if site_title && page_title != site_title
          page_title + TITLE_SEPARATOR + site_title
        elsif site_description && site_title
          site_title + TITLE_SEPARATOR + (site_tagline || site_description)
        else
          page_title || site_title
        end
    end

    def name
      return @name if defined?(@name)
      @name =
        if page_seo["name"] then format_string(page_seo["name"])
        elsif !homepage_or_about? then nil
        elsif site_social["name"] then format_string(site_social["name"])
        elsif site_title then site_title
        end
    end

    def description
      @description ||= format_string(@page["description"] || excerpt) || site_description
    end

    def excerpt
      @item.respond_to?(:excerpt) ? @item.excerpt : nil
    end

    def author
      @author ||= begin
        resolved = [@page["author"], (@page["authors"].first if @page["authors"].is_a?(Array)), @site["author"]]
                   .find { |s| !s.to_s.empty? }
        hash = case resolved
               when Hash then resolved
               when String then { "name" => resolved }
               else {}
               end
        twitter = hash["twitter"] || hash["name"]
        hash.merge("twitter" => (twitter.is_a?(String) ? twitter.sub(/^@/, "") : nil))
      end
    end

    def image
      @image ||= begin
        meta = @page["image"]
        hash = case meta
               when Hash then { "path" => nil }.merge(meta)
               when String then { "path" => meta }
               else { "path" => nil }
               end
        raw = hash["path"] || hash["facebook"] || hash["twitter"]
        if raw
          abs = raw.is_a?(String) && !Utils.absolute_uri?(raw) ? @filters.absolute_url(raw) : raw
          hash.merge("path" => abs)
        end
      end
    end

    def date_modified
      @date_modified ||= begin
        date = page_seo["date_modified"] || @page["last_modified_at"] || @page["date"]
        @filters.date_to_xmlschema(date) if date
      end
    end

    def date_published
      @date_published ||= (@filters.date_to_xmlschema(@page["date"]) if @page["date"])
    end

    def type
      @type ||=
        if page_seo["type"] then page_seo["type"]
        elsif homepage_or_about? then "WebSite"
        elsif @page["date"] then "BlogPosting"
        else "WebPage"
        end
    end

    def links
      page_seo["links"] || (homepage_or_about? ? site_social["links"] : nil)
    end

    def logo
      return unless @site["logo"]
      Utils.absolute_uri?(@site["logo"]) ? @site["logo"] : @filters.absolute_url(@site["logo"])
    end

    def page_locale
      @page_locale ||= (@page["locale"] || @site["locale"] || @page["lang"] || @site["lang"] || "en_US").tr("-", "_")
    end

    def canonical_url
      @canonical_url ||=
        if @page["canonical_url"].to_s.empty?
          @filters.absolute_url(@page["url"]).to_s.gsub(%r{/index\.html$}, "/")
        else
          @page["canonical_url"]
        end
    end

    def json_ld
      author_hash = if author["name"]
                      h = { "@type" => author["type"] || "Person", "name" => author["name"] }
                      h["url"] = author["url"] if author["url"]
                      h
                    end
      image_val = if image
                    image.keys.length == 1 ? image["path"] : image.merge("@type" => "imageObject").tap { |h| h["url"] = h.delete("path") }
                  end
      publisher = if logo
                    p = { "@type" => "Organization", "logo" => { "@type" => "ImageObject", "url" => logo } }
                    p["name"] = author["name"] if author["name"]
                    p
                  end
      main_entity = ({ "@type" => "WebPage", "@id" => canonical_url } if %w[BlogPosting CreativeWork].include?(type))
      {
        "@context" => "https://schema.org",
        "@type" => type,
        "author" => author_hash,
        "dateModified" => date_modified,
        "datePublished" => date_published,
        "description" => description,
        "headline" => page_title,
        "image" => image_val,
        "mainEntityOfPage" => main_entity,
        "name" => name,
        "publisher" => publisher,
        "sameAs" => links,
        "url" => canonical_url,
      }.reject { |_, v| v.nil? }
    end

    private

    def homepage_or_about?
      @page["url"].to_s.match?(HOMEPAGE_OR_ABOUT_REGEX)
    end

    def page_seo
      @page["seo"].is_a?(Hash) ? @page["seo"] : {}
    end

    def site_social
      @site["social"].is_a?(Hash) ? @site["social"] : {}
    end

    def format_string(string)
      return nil if string.nil?
      s = @filters.markdownify(string)
      s = @filters.strip_html(s)
      s = @filters.normalize_whitespace(s)
      s = @filters.escape_once(s)
      s unless s.empty?
    end
  end

  # ---------------------------------------------------------------------------
  # Content objects
  # ---------------------------------------------------------------------------
  class Layout
    attr_reader :name, :data, :body

    def initialize(path)
      @name = File.basename(path, ".*")
      @data, @body = Utils.split_front_matter(Utils.read(path))
      @data ||= {}
    end
  end

  class StaticFile
    attr_reader :site, :path, :relative_path, :collection

    def initialize(site, path, collection = nil)
      @site = site
      @path = path
      @collection = collection
      @relative_path = path.delete_prefix("#{site.source}/")
    end

    def extname
      File.extname(relative_path)
    end

    # Jekyll::StaticFile#url
    def url
      @url ||= begin
        if collection
          cleaned = relative_path.delete_prefix("_#{collection}/").sub(/#{Regexp.escape(extname)}\z/, "")
          Utils.generate_url(site.collection_permalink(collection),
                             "collection" => collection, "path" => cleaned,
                             "output_ext" => "", "name" => "", "title" => "").chomp("/") + extname
        else
          "/#{relative_path}"
        end
      end
    end

    def destination
      File.join(site.dest, CGI.unescape(url))
    end

    def write
      FileUtils.mkdir_p(File.dirname(destination))
      FileUtils.cp(path, destination)
    end

    def to_liquid
      @to_liquid ||= {
        "basename" => File.basename(relative_path, ".*"),
        "name" => File.basename(relative_path),
        "extname" => extname,
        "modified_time" => File.mtime(path),
        "path" => "/#{relative_path}",
      }
    end
  end

  # Shared rendering behaviour for documents and pages.
  module Renderable
    attr_accessor :content, :output

    def markdown?
      MARKDOWN_EXTS.include?(ext)
    end

    def sass?
      SASS_EXTS.include?(ext)
    end

    def output_ext
      if markdown? then ".html"
      elsif sass? then ".css"
      else ext
      end
    end

    def destination
      path = File.join(site.dest, CGI.unescape(url))
      path = File.join(path, "index") if url.end_with?("/")
      path += output_ext unless path.end_with?(output_ext)
      path
    end

    def render
      payload = base_payload
      out = body
      out = site.liquid.render(out, payload, self) if render_with_liquid?
      out = site.markdown.convert(out) if markdown?
      out = site.compile_sass(out, self) if sass?
      self.content = out
      to_liquid["content"] = out
      self.output = place_in_layouts(out, payload)
    end

    def render_with_liquid?
      data.fetch("render_with_liquid", true)
    end

    def place_in_layouts(out, payload)
      layout_name = data["layout"]
      seen = Set.new
      while layout_name && layout_name != "none"
        layout = site.layouts[layout_name.to_s]
        break unless layout
        raise "Layout cycle involving '#{layout_name}'" unless seen.add?(layout_name)
        payload["content"] = out
        payload["layout"] = layout.data
        out = site.liquid.render(layout.body, payload, self)
        layout_name = layout.data["layout"]
      end
      out
    end

    def base_payload
      {
        "site" => site.payload,
        "page" => to_liquid,
        "jekyll" => site.jekyll_payload,
        "paginator" => nil,
      }
    end

    def write
      FileUtils.mkdir_p(File.dirname(destination))
      File.write(destination, output)
    end
  end

  class Document
    include Renderable
    attr_reader :site, :path, :relative_path, :collection, :data, :body

    def initialize(site, path, collection)
      @site = site
      @path = path
      @collection = collection
      @relative_path = path.delete_prefix("#{site.source}/")
      @data, @body = Utils.split_front_matter(Utils.read(path))
      @data ||= {}
      @data["date"] = Utils.parse_date(@data["date"]) if @data.key?("date")

      basename = File.basename(path, ".*")
      slug = basename
      if (m = DATE_FILENAME.match(basename))
        slug = m[2]
        @data["date"] = Utils.parse_date(m[1]) if !@data["date"] || @data["date"].to_i == site.time.to_i
      end
      slug = slug.sub(/\.*\z/, "")
      @data["title"] ||= Utils.titleize_slug(slug)
      @data["slug"] ||= slug
      @data["ext"] ||= ext
    end

    def ext
      File.extname(path)
    end

    def date
      @data["date"] ||= site.time
    end

    def published?
      data.fetch("published", true) && (site.future? || date.to_i <= site.time.to_i)
    end

    # Relative path within the collection directory, without extension.
    def cleaned_relative_path
      rel = relative_path.delete_prefix("_#{collection}/")
      rel.sub(/#{Regexp.escape(File.extname(rel))}\z/, "")
    end

    def url
      @url ||= Utils.generate_url(data["permalink"] || site.collection_permalink(collection), placeholders)
    end

    def placeholders
      {
        "collection" => collection,
        "path" => cleaned_relative_path,
        "output_ext" => output_ext,
        "name" => Utils.slugify(File.basename(path, ".*")),
        "basename" => File.basename(path, ".*"),
        "title" => Utils.slugify(data["slug"]),
        "slug" => Utils.slugify(data["slug"]),
        "categories" => Array(data["categories"]).join("/"),
        "year" => date.strftime("%Y"),
        "month" => date.strftime("%m"),
        "day" => date.strftime("%d"),
        "hour" => date.strftime("%H"),
        "minute" => date.strftime("%M"),
        "second" => date.strftime("%S"),
        "i_day" => date.strftime("%-d"),
        "i_month" => date.strftime("%-m"),
        "short_month" => date.strftime("%b"),
        "short_year" => date.strftime("%y"),
        "y_day" => date.strftime("%j"),
        "week" => date.strftime("%U"),
        "short_day" => date.strftime("%a"),
      }
    end

    def to_liquid
      @to_liquid ||= data.merge(
        "content" => nil,
        "output" => nil,
        "path" => relative_path,
        "relative_path" => relative_path,
        "url" => url,
        "id" => url.chomp("/"),
        "collection" => collection,
        "date" => date,
        "draft" => false,
      )
    end

    def <=>(other)
      cmp = date <=> other.date
      cmp = path <=> other.path if cmp.nil? || cmp.zero?
      cmp
    end

    # Jekyll::Excerpt: first paragraph (before "\n\n") plus any reference-style
    # link/footnote definitions from the rest, rendered through Liquid+Markdown.
    LIQUID_TAG_REGEX = /{%-?\s*(\w+)\s*.*?-?%}/m.freeze
    LINK_REF_REGEX = /^ {0,3}\[[^\]]+\]:.+$/.freeze

    def excerpt
      return @excerpt if defined?(@excerpt)
      separator = data["excerpt_separator"] || site.config.fetch("excerpt_separator", "\n\n")
      return @excerpt = nil if separator.to_s.empty?

      text = extract_excerpt(separator)
      payload = base_payload
      out = render_with_liquid? ? site.liquid.render(text, payload, self) : text
      out = site.markdown.convert(out) if markdown?
      @excerpt = out
    end

    private

    def extract_excerpt(separator)
      head, _, tail = body.to_s.partition(separator)
      return head if tail.empty?
      head = sanctify_liquid_tags(head.dup) if head.include?("{%")
      definitions = tail.scan(LINK_REF_REGEX)
      return head if definitions.empty?
      head + "\n\n" + definitions.join("\n")
    end

    def sanctify_liquid_tags(head)
      head.scan(LIQUID_TAG_REGEX).flatten.reverse_each do |tag_name|
        tag = Liquid::Template.tags[tag_name]
        next unless tag && tag.ancestors.include?(Liquid::Block)
        next if head.match?(/{%-?\s*end#{tag_name}\s*-?%}/)
        head << "\n{% end#{tag_name} %}"
      end
      head
    end
  end

  class Page
    include Renderable
    attr_reader :site, :path, :relative_path, :data, :body, :dir, :name

    # `body:`/`data:` allow generated pages (sitemap.xml, robots.txt).
    def initialize(site, relative_path, body: nil, data: nil)
      @site = site
      @relative_path = relative_path
      @path = File.join(site.source, relative_path)
      @dir = File.dirname(relative_path)
      @dir = "" if @dir == "."
      @name = File.basename(relative_path)
      if body
        @data = data || {}
        @body = body
      else
        @data, @body = Utils.split_front_matter(Utils.read(@path))
        @data ||= {}
      end
    end

    def ext
      File.extname(name)
    end

    def basename
      File.basename(name, ".*")
    end

    def html?
      output_ext == ".html"
    end

    def index?
      basename == "index"
    end

    # Jekyll::Page#template + Utils.add_permalink_suffix
    def template
      return "/:path/:basename:output_ext" unless html?
      return "/:path/" if index?
      style = site.config["permalink"].to_s
      case style
      when "pretty" then "/:path/:basename/"
      when "date", "ordinal", "none" then "/:path/:basename:output_ext"
      else
        t = "/:path/:basename"
        t += "/" if style.end_with?("/")
        t += ":output_ext" if style.end_with?(":output_ext")
        t
      end
    end

    def url
      @url ||= Utils.generate_url(
        data["permalink"] || template,
        "path" => dir, "basename" => basename, "output_ext" => output_ext
      )
    end

    def to_liquid
      @to_liquid ||= data.merge(
        "content" => nil,
        "dir" => url.end_with?("/") ? url : File.dirname(url),
        "name" => name,
        "path" => data.fetch("path", relative_path),
        "url" => url,
      )
    end
  end

  # ---------------------------------------------------------------------------
  # Liquid environment
  # ---------------------------------------------------------------------------
  class LiquidEnv
    def initialize(site)
      @site = site
      Liquid::Template.error_mode = :warn
      Liquid::Template.register_tag("include", IncludeTag)
      Liquid::Template.register_tag("seo", SeoTag)
      Liquid::Template.register_tag("link", LinkTag)
      Liquid::Template.register_filter(Filters)
    end

    def render(text, payload, item)
      template = Liquid::Template.parse(text, line_numbers: true)
      template.render!(payload, registers: { site: @site, page: item }, strict_filters: false, strict_variables: false)
    rescue Liquid::Error => e
      raise "Liquid error in #{item&.relative_path}: #{e.message}"
    end
  end

  # ---------------------------------------------------------------------------
  # Site
  # ---------------------------------------------------------------------------
  class Site
    attr_reader :source, :dest, :config, :time, :env, :layouts, :markdown, :liquid,
                :collections, :pages, :static_files

    def initialize(source:, dest:, future: false, env: "development")
      @source = File.expand_path(source)
      @dest = File.expand_path(dest)
      config_path = File.join(@source, "_config.yml")
      @config = File.exist?(config_path) ? (YAML.safe_load(Utils.read(config_path), permitted_classes: [Date, Time], aliases: true) || {}) : {}
      @config["future"] = true if future
      @config["collections"] = { "posts" => {} }.merge(@config["collections"] || {})
      @config["collections"].each_value { |c| c ||= {} }
      ENV["TZ"] = @config["timezone"] if @config["timezone"]
      @time = Time.now
      @env = env
      @markdown = Markdown.new(@config)
      @liquid = LiquidEnv.new(self)
      @include_cache = {}
      @extra_outputs = []
      @written = Set.new
    end

    def future?
      @config["future"] == true
    end

    def jekyll_payload
      { "version" => JEKYLL_VERSION, "environment" => env }
    end

    def collection_permalink(label)
      if label == "posts"
        style = @config["permalink"] || "date"
        POST_PERMALINK_STYLES.fetch(style, style)
      else
        (@config["collections"][label] || {})["permalink"] || "/:collection/:path:output_ext"
      end
    end

    def collection_output?(label)
      label == "posts" || (@config["collections"][label] || {})["output"] == true
    end

    def find_item(relative_path)
      @items_by_path ||= (@collections.values.flatten + @pages + @static_files)
                         .each_with_object({}) { |i, h| h[i.relative_path] = i }
      @items_by_path[relative_path]
    end

    def include_template(path)
      @include_cache[path] ||= Liquid::Template.parse(Utils.read(path), line_numbers: true)
    end

    # --- reading --------------------------------------------------------------

    def read
      @layouts = {}
      Dir.glob(File.join(source, "_layouts", "*")).sort.each do |f|
        next unless File.file?(f)
        layout = Layout.new(f)
        @layouts[layout.name] = layout
      end

      @collections = {}
      @collection_static_files = []
      @config["collections"].each_key do |label|
        dir = File.join(source, "_#{label}")
        docs = []
        if File.directory?(dir)
          Dir.glob(File.join(dir, "**", "*")).sort.each do |f|
            next unless File.file?(f)
            unless Utils.has_front_matter?(f)
              @collection_static_files << StaticFile.new(self, f, label)
              next
            end
            doc = Document.new(self, f, label)
            docs << doc if doc.published?
          end
        end
        @collections[label] = docs.sort
      end

      @pages = []
      @static_files = []
      walk("")
      @static_files.concat(@collection_static_files)
      @pages.sort_by!(&:name)
      @static_files.sort_by!(&:relative_path)
    end

    def excludes
      @excludes ||= DEFAULT_EXCLUDES + Array(@config["exclude"])
    end

    def includes
      @includes ||= Array(@config["include"])
    end

    def walk(dir)
      base = File.join(source, dir)
      entries = Dir.children(base).sort
      dirs, pages, statics = [], [], []
      entries.each do |entry|
        rel = dir.empty? ? entry : File.join(dir, entry)
        full = File.join(base, entry)
        next if full == dest || excluded?(entry, rel, full)
        if File.directory?(full) then dirs << rel
        elsif Utils.has_front_matter?(full) then pages << rel
        else statics << rel
        end
      end
      dirs.each { |d| walk(d) }
      pages.each { |p| @pages << Page.new(self, p) }
      statics.each { |s| @static_files << StaticFile.new(self, File.join(source, s)) }
    end

    def excluded?(entry, rel, full)
      return true if entry.end_with?("~")
      return false if glob_include?(includes, rel, full)
      return true if glob_include?(excludes, rel, full)
      entry.start_with?(".", "_", "#")
    end

    def glob_include?(patterns, rel, full)
      is_dir = File.directory?(full)
      patterns.any? do |pattern|
        pattern_full = File.join(source, pattern)
        File.fnmatch?(pattern_full, full, File::FNM_DOTMATCH | File::FNM_EXTGLOB) ||
          full.start_with?(pattern_full) ||
          (is_dir && pattern_full == "#{full}/") ||
          File.fnmatch?(pattern, rel, File::FNM_DOTMATCH | File::FNM_EXTGLOB)
      end
    end

    # --- liquid payload -------------------------------------------------------

    def payload
      @payload ||= begin
        h = @config.dup
        h["time"] = time
        h["posts"] = @collections["posts"].sort { |a, b| b <=> a }.map(&:to_liquid)
        @collections.each { |label, docs| h[label] = docs.map(&:to_liquid) unless label == "posts" }
        h["documents"] = @collections.values.flatten.map(&:to_liquid)
        h["pages"] = @pages.map(&:to_liquid)
        h["html_pages"] = @pages.select { |p| p.html? || p.url.end_with?("/") }.map(&:to_liquid)
        h["static_files"] = @static_files.map(&:to_liquid)
        h["collections"] = @collections.keys.sort.map do |label|
          cfg = @config["collections"][label] || {}
          cfg.merge(
            "label" => label,
            "output" => collection_output?(label),
            "docs" => @collections[label].map(&:to_liquid),
          )
        end
        h["categories"] = {}
        h["tags"] = {}
        h["data"] = {}
        h
      end
    end

    # --- sass -----------------------------------------------------------------

    def compile_sass(text, item)
      load_paths = [File.join(source, "_sass")]
      begin
        require "sass-embedded"
      rescue LoadError
        nil
      end
      style = (config.dig("sass", "style") || "expanded").to_s.delete_prefix(":")
      style = "expanded" unless %w[expanded compressed].include?(style)
      if defined?(::Sass) && ::Sass.respond_to?(:compile_string)
        syntax = item.ext == ".sass" ? :indented : :scss
        result = ::Sass.compile_string(text, syntax: syntax, style: style.to_sym, load_paths: load_paths,
                                             charset: true, source_map: true, source_map_include_sources: true,
                                             url: "file://#{item.path}")
        css = result.css
        map = JSON.parse(result.source_map)
        map["file"] = "#{item.basename}.css"
        root = "file://#{File.dirname(item.path)}/"
        map["sources"].map! { |src| src.start_with?("file:") ? src.delete_prefix(root) : src }
        add_extra_output(File.join(File.dirname(item.destination), "#{item.basename}.css.map"), JSON.generate(map))
        css + "#{style == "compressed" ? "" : "\n\n"}/*# sourceMappingURL=#{item.basename}.css.map */"
      elsif (bin = ENV["PATH"].split(File::PATH_SEPARATOR).map { |d| File.join(d, "sass") }.find { |f| File.executable?(f) }) &&
            (out = compile_sass_cli(bin, item, text, style, load_paths))
        out
      else
        warn "minijekyll: no Sass compiler found; copying #{item.relative_path} unmodified"
        text
      end
    end

    # Fallback for when the sass-embedded gem is not loadable but a `sass`
    # executable (dart-sass / npm sass-embedded) is on PATH. Mirrors the gem
    # path above: compressed CSS + a source map whose sources are relative to
    # the stylesheet's directory. Returns nil if the executable fails.
    def compile_sass_cli(bin, item, text, style, load_paths)
      Dir.mktmpdir("minijekyll-sass") do |tmp|
        src = File.join(tmp, item.name)
        out = File.join(tmp, "#{item.basename}.css")
        File.write(src, text)
        args = [bin, src, out, "--style=#{style}", "--source-map", "--embed-sources", "--load-path=#{load_paths.first}"]
        err = IO.popen(args, err: [:child, :out], &:read)
        unless $?.success? && File.exist?(out)
          warn "minijekyll: `#{bin}` failed:\n#{err}"
          return nil
        end
        css = File.read(out, encoding: "utf-8").sub(%r{\n?/\*# sourceMappingURL=.*\*/\s*\z}, "")
        map = JSON.parse(File.read("#{out}.map"))
        map.delete("file") # re-added last, matching jekyll-sass-converter's key order
        map["file"] = "#{item.basename}.css"
        map["sources"].map! { |s| s == item.name || s.end_with?("/#{item.name}") ? item.name : s }
        add_extra_output(File.join(File.dirname(item.destination), "#{item.basename}.css.map"), JSON.generate(map))
        css + "#{style == "compressed" ? "" : "\n\n"}/*# sourceMappingURL=#{item.basename}.css.map */"
      end
    end

    def add_extra_output(path, content)
      @extra_outputs << [path, content]
    end

    # --- generated pages (jekyll-sitemap) ------------------------------------

    SITEMAP_MINIFY = /(?<=>\n|})\s+/.freeze
    SITEMAP_EXTENSIONS = %w[.htm .html .xhtml .pdf].freeze
    SITEMAP_TEMPLATE = <<~'XML'
      <?xml version="1.0" encoding="UTF-8"?>
      {% if page.xsl %}
        <?xml-stylesheet type="text/xsl" href="{{ "/sitemap.xsl" | absolute_url }}"?>
      {% endif %}
      <urlset xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://www.sitemaps.org/schemas/sitemap/0.9 http://www.sitemaps.org/schemas/sitemap/0.9/sitemap.xsd" xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
        {% assign collections = site.collections | where_exp:'collection','collection.output != false' %}
        {% for collection in collections %}
          {% assign docs = collection.docs | where_exp:'doc','doc.sitemap != false' %}
          {% for doc in docs %}
            <url>
              <loc>{{ doc.url | replace:'/index.html','/' | absolute_url | xml_escape }}</loc>
              {% if doc.last_modified_at or doc.date %}
                <lastmod>{{ doc.last_modified_at | default: doc.date | date_to_xmlschema }}</lastmod>
              {% endif %}
            </url>
          {% endfor %}
        {% endfor %}

        {% assign pages = site.html_pages | where_exp:'doc','doc.sitemap != false' | where_exp:'doc','doc.url != "/404.html"' %}
        {% for page in pages %}
          <url>
            <loc>{{ page.url | replace:'/index.html','/' | absolute_url | xml_escape }}</loc>
            {% if page.last_modified_at %}
              <lastmod>{{ page.last_modified_at | date_to_xmlschema }}</lastmod>
            {% endif %}
          </url>
        {% endfor %}

        {% assign static_files = page.static_files | where_exp:'page','page.sitemap != false' | where_exp:'page','page.name != "404.html"' %}
        {% for file in static_files %}
          <url>
            <loc>{{ file.path | replace:'/index.html','/' | absolute_url | xml_escape }}</loc>
            <lastmod>{{ file.modified_time | date_to_xmlschema }}</lastmod>
          </url>
        {% endfor %}
      </urlset>
    XML
    ROBOTS_TEMPLATE = "Sitemap: {{ \"sitemap.xml\" | absolute_url }}\n"

    def generate_sitemap
      return if url_taken?("/sitemap.xml")
      html_statics = @static_files.select { |f| SITEMAP_EXTENSIONS.include?(f.extname) }.map(&:to_liquid)
      @pages << Page.new(self, "sitemap.xml",
                         body: SITEMAP_TEMPLATE.gsub(SITEMAP_MINIFY, ""),
                         data: { "layout" => nil, "static_files" => html_statics, "xsl" => source_has?("sitemap.xsl") })
      return if url_taken?("/robots.txt")
      @pages << Page.new(self, "robots.txt", body: ROBOTS_TEMPLATE, data: { "layout" => nil })
    end

    def source_has?(name)
      File.exist?(File.join(source, name))
    end

    def url_taken?(url)
      (@pages + @static_files).any? { |p| p.url == url }
    end

    # --- build ----------------------------------------------------------------

    def build
      read
      generate_sitemap
      payload # freeze the site payload before rendering

      renderables = @collections.values.flatten + @pages
      renderables.each do |item|
        item.render
        item.write
        @written << item.destination
      end
      @static_files.each do |f|
        f.write
        @written << f.destination
      end
      @extra_outputs.each do |path, content|
        FileUtils.mkdir_p(File.dirname(path))
        File.write(path, content)
        @written << path
      end
      clean
      renderables.size
    end

    # Remove files in dest that this build did not produce (keeps .git).
    def clean
      Dir.glob(File.join(dest, "**", "*"), File::FNM_DOTMATCH).sort.reverse_each do |f|
        base = File.basename(f)
        next if base == "." || base == ".."
        next if f.start_with?(File.join(dest, ".git"))
        if File.directory?(f)
          Dir.rmdir(f) if Dir.empty?(f)
        elsif !@written.include?(f)
          File.delete(f)
        end
      end
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { source: Dir.pwd, dest: nil, future: false, env: ENV.fetch("JEKYLL_ENV", "development") }
  OptionParser.new do |o|
    o.banner = "Usage: ruby minijekyll.rb [options]"
    o.on("-s", "--source DIR", "Source directory (default: cwd)") { |v| options[:source] = v }
    o.on("-d", "--dest DIR", "Destination directory (default: SOURCE/_site)") { |v| options[:dest] = v }
    o.on("--future", "Publish posts with dates in the future") { options[:future] = true }
    o.on("--env ENV", "Value for jekyll.environment (default: $JEKYLL_ENV or development)") { |v| options[:env] = v }
  end.parse!
  options[:dest] ||= File.join(options[:source], "_site")

  start = Time.now
  site = MiniJekyll::Site.new(**options)
  count = site.build
  printf("minijekyll: rendered %d documents and copied %d static files to %s in %.2fs\n",
         count, site.static_files.size, site.dest, Time.now - start)
end
