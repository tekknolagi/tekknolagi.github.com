<ul>
{% for post in site.blog_lisp %}
{% if post.index != true %}
  <li><div class="post-date"><i>{{ post.date | date: '%B %-d, %Y' }}</i></div>
      <a href="{{ post.url }}">{{ post.title }}</a></li>
{% endif %}
{% endfor %}
</ul>

And more to come.
