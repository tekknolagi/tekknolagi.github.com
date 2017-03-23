<ul>
{% for post in site.blog_lisp %}
{% if post.index != true %}
    <li>
        <div class="post-date"><i>{{ post.date | date: '%B %-d, %Y' }}</i></div>
        <a href="{{ post.url }}">{{ post.title }}</a>
        <!-- <div><i>{{ post.content | number_of_words | divided_by: 100 }} minute read</i></div> -->
    </li>
{% endif %}
{% endfor %}
</ul>

And more to come.
