---
layout: default
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

I found this website called [LibraryThing](https://www.librarything.com/home)
that allows you to catalog, organize, and search your library -- and many
others. The following button links to my profile.

<a href="http://www.librarything.com/profile/tekknolagi">
    <img src="/assets/img/librarything.png" />
</a>

Here is some of my favorite music:

<ul>
  {% for composer in site.data.classical_composers %}
    {% for piece in composer.pieces %}
      <li>
        <i>{{ piece.name }}</i> by {{ composer.composer }}{% if piece.conductor %}, conducted by {{ piece.conductor }}{% endif %}{% if piece.director %}, directed by {{ piece.director }}{% endif %}
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

Here are some websites I like:

<ul>
  {% for website in site.data.sites %}
  <li><a href="http://{{ website.url }}">{{ website.url }}</a></li>
  {% endfor %}
</ul>

In particular, I enjoy the data density and easy readability of the NASA page.

My favorite tea is currently {{ site.data.other.tea.maker }}'s
<i>{{ site.data.other.tea.name }}</i>.

<p>
I took some wonderful classes while I was at Tufts. I wholeheartedly recommend
the following:
</p>
<ul>
    {% for class in site.data.classes %}
    <li><i>{{ class.name }}</i> with {{ class.prof }}, {{ class.desc }}</li>
    {% endfor %}
</ul>

I like music featuring the [hammered dulcimer](https://en.wikipedia.org/wiki/Hammered_dulcimer).
Below is a collection of music (mostly film scores) that includes at least a
little bit of hammered dulcimer in it:

* *Hary Janos* (Zoltán Kodály), where I first heard it
* Various tracks from The Grand Budapest Hotel (Alexandre Desplat), such as:
  * *Mr. Moustafa*
  * *A Prayer for Madame D*
  * *Daylight Express to Lutz*
  * ... and many more
* *Discombobulate* (Hans Zimmer), from Sherlock Holmes -- the Robert Downey Jr
  version
* *The Dragon Book* (John Powell), from How to Train Your Dragon
* Various tracks from The Man From U.N.C.L.E (Daniel Pemberton)
  * *His Name Is Napoleon Solo*
  * *Signori Toileto Italiano*
  * *Laced Drinks*
  * *Drums of War*
* *Snow Plane* (Thomas Newman), from Spectre

Thanks to [this website][cimbalom0] for linking some that were harder for me to
find (and others not included here). Also thanks to [this website][cimbalom1]
with help identifying tracks in The Man From U.N.C.L.E. As a sidebar, I would
one day also like to catalogue music that has a sort of horse
galloping/trotting feel found in *Breaking Out* (U.N.C.L.E) or *Two Mules for
Sister Sara* (Sherlock Holmes).

[cimbalom0]: https://web.archive.org/web/20200615181712/https://manufacturing.dustystrings.com/blog/hammered-dulcimer-film-scores
[cimbalom1]: http://web.archive.org/web/20191228224428/https://moviemusicuk.us/2015/08/17/the-man-from-u-n-c-l-e-daniel-pemberton/
