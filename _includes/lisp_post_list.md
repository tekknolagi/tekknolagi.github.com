<ul>
    {% for post in site.blog_lisp %}
    {% if post.index != true %}
    <li><a href="{{ post.url }}">{{ post.title }}</a></li>
    {% endif %}
    {% endfor %}
</ul>
