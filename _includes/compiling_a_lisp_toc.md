#### Mini Table of Contents

<ul>
{% assign posts_reverse = site.posts | reverse %}
{% for post in posts_reverse %}
{% if post.title contains "Compiling a Lisp" and post.index != true and post.draft != true %}
    <li>
        <a href="{{ post.url }}"><span>{{ post.title | remove_first: "Compiling a Lisp: " }}</span></a>
        {% if post.title == page.title %} <span style="color: red"><i>(this page)</i></span> {% endif %}
    </li>
{% endif %}
{% endfor %}
</ul>
