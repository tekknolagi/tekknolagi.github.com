<ul>
    {% assign posts_chrono = site.posts | where post.series == "compiling-a-lisp" and post.draft != true | reverse %}
    {% for post in posts_chrono %}
    <li class="post-item">
        <a class="post-title" href="{{ post.url }}"><span>{{ post.title }}</span></a>
        <div class="post-date"><i>{{ post.date | date: '%B %-d, %Y' }}</i></div>
    </li>
    {% endfor %}
</ul>
