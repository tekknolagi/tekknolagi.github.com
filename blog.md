---
title: Blog
layout: page
blog_index: true
permalink: blog/
---

<ul>
    {% for post in site.posts %}
    {% unless post.draft == true or post.series %}
    <li class="post-item">
        <a class="post-title" href="{{ post.url }}"><span>{{ post.title }}</span></a>
        <div class="post-date"><i>{{ post.date | date: '%B %-d, %Y' }}</i></div>
    </li>
    {% endunless %}
    {% endfor %}
</ul>

## Runtime optimization, the series
<ul>
    {% assign posts_chrono = site.posts | where: "series","runtime-opt" | reverse %}
    {% for post in posts_chrono %}
    <li class="post-item">
        <a class="post-title" href="{{ post.url }}"><span>{{ post.title }}</span></a>
        <div class="post-date"><i>{{ post.date | date: '%B %-d, %Y' }}</i></div>
    </li>
    {% endfor %}
</ul>

## Compiling a Lisp, the series
{% include compiling_a_lisp.md %}

## Writing a Lisp, the series
{% include writing_a_lisp.md %}
