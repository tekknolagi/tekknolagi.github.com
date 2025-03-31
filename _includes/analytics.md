{% if jekyll.environment == 'production' %}
<!-- Google tag (gtag.js) -->
<script async src="https://www.googletagmanager.com/gtag/js?id=G-MNTD6DM8MP"></script>
<script>
if (location.hostname === "bernsteinbear.com") {
  window.dataLayer = window.dataLayer || [];
  function gtag(){dataLayer.push(arguments);}
  gtag('js', new Date());

  gtag('config', 'G-MNTD6DM8MP');
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
document.querySelectorAll("a").forEach(a => {
    let url = new URL(a.href);
    if (url.hostname === "bernsteinbear.com") {
        url.searchParams.append("utm_campaign", "april_fools_2025");
        a.href = url.toString();
    }
});
</script>
<script async src="/assets/js/count.js"></script>
{% endif %}
