<!-- Light mode by default -->
{% capture styles %}
{% include main.css %}
{% endcapture %}
<style>
{{ styles | scssify }}
</style>
