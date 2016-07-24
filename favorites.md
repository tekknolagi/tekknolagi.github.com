---
layout: post
---
Here are some of my favorite personal websites:

<ul>
  {% for website in site.data.sites %}
  <li><a href="http://{{ website.url }}/">{{ website.url }}</a></li>
  {% endfor %}
</ul>
