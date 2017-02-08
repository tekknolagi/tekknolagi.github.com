<ul>
{% assign posts = site.blog_lisp | where: 'index', nil %}
{% for post in posts %}
  <li><div class="post-date"><i>{{ post.date | date: '%B %-d, %Y' }}</i></div>
      <a href="{{ post.url }}">{{ post.title }}</a></li>
{% endfor %}
</ul>
