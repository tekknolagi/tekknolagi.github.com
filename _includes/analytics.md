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
    let code = location.hostname == 'bernsteinbear.com' ? 'tekknolagi' : 'no';
    window.goatcounter = {
        endpoint: 'https://' + code + '.goatcounter.com/count',
    }
</script>
<script async src="/assets/js/count.js"></script>
{% endif %}
