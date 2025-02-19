    <hr>
    <div id="footer">
      <p>
        Max Bernstein's Blog is proudly powered by
        <a href="http://wordpress.org/" target="_blank">WordPress</a>
        <br><a href="/feed.xml">Entries (RSS)</a>
        <br />
        <marquee style="max-width: 75%;">STOP THE WAR! Use whatever text editor you like. Even ed, if you must.</marquee>
      </p>
    </div>
    <!-- Workaround for FB MITM -->
    <span id="iab-pcm-sdk"></span><span id="iab-autofill-sdk"></span>
    {% include analytics.md %}
    <script>
    if ("{{ page.url }}" == window.location.pathname) {
        let url = "/page.shtml?url=" + window.location.pathname;
        if (window.location.search) {
            url += encodeURIComponent(window.location.search);
        }
        window.history.replaceState(null, null, url);
    }
    </script>
