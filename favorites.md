---
layout: post
title: Favorite Things
---

Here are some of my favorite personal websites:

<ul>
  {% for website in site.data.sites %}
  <li><a href="http://{{ website.url }}/">{{ website.url }}</a></li>
  {% endfor %}
</ul>

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

My favorite tea is currently {{ site.data.other.tea.maker }}'s
<i>{{site.data.other.tea.name}}</i>.
