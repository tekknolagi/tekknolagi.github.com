<ul>
{% assign posts_reverse = site.posts | reverse %}
{% for post in posts_reverse %}
{% if post.title contains "Compiling a Lisp" and post.index != true and post.draft != true %}
    <li class="post-item">
        <a class="post-title" href="{{ post.url }}"><span>{{ post.title }}</span></a>
        <div class="post-date"><i>{{ post.date | date: '%B %-d, %Y' }}</i></div>
        <!-- <div><i>{{ post.content | number_of_words | divided_by: 100 }} minute read</i></div> -->
    </li>
{% endif %}
{% endfor %}
</ul>
