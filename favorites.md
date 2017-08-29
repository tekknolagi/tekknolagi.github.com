---
layout: post
title: Favorite Things
---

<!--
Here are some of my favorite books:

<ul>
  {% for book in site.data.books %}
  <li><i>{{ book.title }}</i> by {{ book.author }}</li>
  {% endfor %}
</ul>

Here are some of my favorite films:

<ul>
  {% for film in site.data.films %}
  <li>{{ film.title }}</li>
  {% endfor %}
</ul>
-->

Here is some of my favorite music:

<ul>
  {% for composer in site.data.classical_composers %}
    {% for piece in composer.pieces %}
      <li>
        <i>{{ piece.name }}</i> by {{ composer.composer }}{% if piece.conductor %}, conducted by {{ piece.conductor }}{% endif %}
      </li>
    {% endfor %}
  {% endfor %}
</ul>

I am currently listening to some newer music, too:

<ul>
  {% for item in site.data.other.current_music %}
    <li>
    {% if item.smallcaps %}
    <i style="font-variant: small-caps;">
    {% else %}
    <i>
    {% endif %}
    {{ item.group.name }}</i>
    {% if item.group.english_name %}
    <i>({{ item.group.english_name }})</i>
    {% endif %}
    </li>
  {% endfor %}
</ul>

Here's some awesomely weird music that I found:

<ul>
  {% for item in site.data.other.cool_artists %}
    <li>
      <a href="{{ item.group.link }}">{{ item.group.name }}</a>
    </li>
  {% endfor %}
</ul>

Here are some websites I think look good:

<ul>
  {% for website in site.data.sites %}
  <li><a href="http://{{ website.url }}/">{{ website.url }}</a></li>
  {% endfor %}
</ul>

In particular, I enjoy the data density and easy readability of the NASA page.

My favorite tea is currently {{ site.data.other.tea.maker }}'s
<i>{{ site.data.other.tea.name }}</i>.
