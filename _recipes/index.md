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

## External recipes

Tried, enjoyed, and repeated frequently by yours truly.

* [Bibimbap](https://www.maangchi.com/recipe/bibimbap) by Maangchi
* [Adasi](http://www.ahueats.com/2015/02/adasi-persian-lentil-stew.html) by Ahu
  Eats
  * Goes well with brown rice or basmati rice
* [Vegan adobo](https://thewoksoflife.com/vegan-adobo/) by The Woks of Life
* Koshari
  * I have yet to find a good published recipe for this. But just try it. It's
    fantastic.
* [Mongolian soy curls](https://thevietvegan.com/vegan-mongolian-beef/) by Lisa
  Le
* [Sweet potato tacos](https://playswellwithbutter.com/roasted-sweet-potato-cauliflower-tacos/)
  by Jess
* [Salad Shirazi](http://www.ahueats.com/2016/06/salad-shirazi.html) by Ahu
  Eats
* [Fattoush](http://www.ahueats.com/2016/06/fattoush-salad.html) by Ahu Eats
* [Jollof rice](https://cooking.nytimes.com/recipes/1018069-jollof-rice) by
  Tunde Wey with [tofu and
  plantains](https://www.vegannigerian.com/2015/02/sauced-tofu-and-plantainguest-post.html)
  by 9jafoodie
* [Rice cooker congee](https://www.freshnessgf.com/rice-cooker-congee/) by Ness
  * To simplify, use 1:7 for rice:water. Add ginger, stock, tamari. Cook and
    stir 45 mins. Enjoy.
* [Cheesy Cabbage Tteokbokki](https://web.archive.org/web/20220616091010/https://cooking.nytimes.com/recipes/1023237-cheesy-cabbage-tteokbokki)
  (also visible [here](https://www.justtherecipe.com/?url=https://cooking.nytimes.com/recipes/1023237-cheesy-cabbage-tteokbokki))
  * One pot, delicious

## Other text-based recipes

See [based.cooking](https://based.cooking/) and
[grimgrains.com](https://grimgrains.com/site/home.html).
