---
title: Recipes
permalink: /recipes/
index: true
layout: page
---

<ul>
    {% for recipe in site.recipes %}
    {% if recipe.index != true %}
    <li><a href="{{ recipe.url }}">{{ recipe.title }}</a></li>
    {% endif %}
    {% endfor %}
</ul>
