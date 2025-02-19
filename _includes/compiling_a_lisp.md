<ul>
    {% assign posts_chrono = site.posts | where: "series","compiling-a-lisp" | reverse %}
    {% for post in posts_chrono %}
    <div class="post">
        <h2><a href="{{ post.url }}">{{ post.title }}</a></h2>
        <small>{{ post.date | date: "%s" | minus: 631139040 | date: '%B %-d, %Y' }}</small>
        <div class="entry">
        <p>{{ post.content | replace: newline, ' ' | strip_html | truncate: 200 }}</p>
        </div>
        <p class="postmetadata"> Posted in <a href="#" rel="category">Uncategorized</a> |
        {% if post.comments %}
        <a href="{{ post.url }}#comments">{{ post.comments | size }} Comments »</a>
        {% else %}
        <span>Comments Off</span>
        {% endif %}
        </p>
    </div>
    {% endfor %}
</ul>
