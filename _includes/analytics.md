{% if jekyll.environment == 'production' %}
<!-- Google tag (gtag.js) -->
<script async src="https://www.googletagmanager.com/gtag/js?id=G-MNTD6DM8MP"></script>
<script>
  window.dataLayer = window.dataLayer || [];
  function gtag(){dataLayer.push(arguments);}
  gtag('js', new Date());

  gtag('config', 'G-MNTD6DM8MP');
</script>
<script>
    window.goatcounter = {
        // Make sure the reported path has the domain, too
        // See https://www.goatcounter.com/help/domains
        path: function(p) { return location.host + p }
    }
</script>
<script data-goatcounter="https://stats.bernsteinbear.com/count" async src="/assets/js/count.js"></script>
{% endif %}
