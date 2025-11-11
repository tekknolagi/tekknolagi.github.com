    <footer>
      <hr />
      This blog is <a href="{{ site.repo.repository_url }}">open source</a>.
      See an error? Go ahead and
      <a href="{{ site.repo.repository_url }}/edit/{{ site.repo.branch }}/{{ page.path }}" title="Help improve {{ page.path }}">propose a change</a>.
      <br />
      <img style="padding-top: 5px;" src="/assets/img/banner.png" />
      <a href="https://notbyai.fyi/"><img style="padding-top: 5px;" src="/assets/img/notbyai.svg" /></a>
      <a href="{{ site.rc_scout }}">
        <span style="display: inline-block;">
          <object data="/assets/img/rc-logo.svg" type="image/svg+xml" style="pointer-events: none; height: 42px; width: 33.6px;"></object>
        </span>
      </a>
    </footer>
    <!-- Workaround for FB MITM -->
    <span id="iab-pcm-sdk"></span><span id="iab-autofill-sdk"></span>
    {% include analytics.md %}
