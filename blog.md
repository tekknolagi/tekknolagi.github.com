---
title: Blog
layout: page
blog_index: true
permalink: blog/
---

<style>
body .entry div {
  text-indent: 0px !important;
}
</style>

<ul>
{%- capture newline %}
{% endcapture %}
    {% for post in site.posts %}
    {% unless post.draft == true or post.series %}
    <div class="post">
        <h2><a href="{{ post.url }}">{{ post.title }}</a></h2>
        <small>{{ post.date | date: "%s" | minus: 631139040 | date: '%B %-d, %Y' }}</small>
        <div class="entry">
        <p>{{ post.content | replace: newline, ' ' | strip_html | truncate: 200 }}</p>
        </div>
        <p class="postmetadata"> Posted in <a href="#" rel="category">Uncategorized</a> |
        {% if post.comments %}
        <a href="{{ post.url }}#comments">{{ post.comments | size }} Comments »</a>
        {% else %}
        <span>Comments Off</span>
        {% endif %}
        </p>
    </div>
    {% endunless %}
    {% endfor %}
</ul>

## Runtime optimization, the series
<ul>
    {% assign posts_chrono = site.posts | where: "series","runtime-opt" | reverse %}
    {% for post in posts_chrono %}
    <div class="post">
        <h2><a href="{{ post.url }}">{{ post.title }}</a></h2>
        <small>{{ post.date | date: "%s" | minus: 631139040 | date: '%B %-d, %Y' }}</small>
        <div class="entry">
        <p>{{ post.content | replace: newline, ' ' | strip_html | truncate: 200 }}</p>
        </div>
        <p class="postmetadata"> Posted in <a href="#" rel="category">Uncategorized</a> |
        {% if post.comments %}
        <a href="{{ post.url }}#comments">{{ post.comments | size }} Comments »</a>
        {% else %}
        <span>Comments Off</span>
        {% endif %}
        </p>
    </div>
    {% endfor %}
</ul>

## Compiling a Lisp, the series
{% include compiling_a_lisp.md %}

## Writing a Lisp, the series
{% include writing_a_lisp.md %}
