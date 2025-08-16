{% if jekyll.environment == 'production' and page.xslt != true %}
<!-- Google tag (gtag.js) -->
<script async src="https://www.googletagmanager.com/gtag/js?id=G-MNTD6DM8MP"></script>
<script>
if (location.hostname === "bernsteinbear.com") {
  window.dataLayer = window.dataLayer || [];
  function gtag(){dataLayer.push(arguments);}
  gtag('js', new Date());

  gtag('config', 'G-MNTD6DM8MP');
} else {
  alert("Please remove all mentions of me (socials, Google Analytics, GoatCounter, ...) from your fork of my website. Thanks!");
}
</script>
<script>
if (location.hostname === "bernsteinbear.com") {
    let code = location.hostname == 'bernsteinbear.com' ? 'tekknolagi' : 'no';
    window.goatcounter = {
        endpoint: 'https://' + code + '.goatcounter.com/count',
    }
} else {
  alert("Please remove all mentions of me (socials, Google Analytics, GoatCounter, ...) from your fork of my website. Thanks!");
}
</script>
<script async src="/assets/js/count.js"></script>
{% endif %}
