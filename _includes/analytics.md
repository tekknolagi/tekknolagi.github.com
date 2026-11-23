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
    !function(t,e){var o,n,p,r;e.__SV||(window.posthog && window.posthog.__loaded)||(window.posthog=e,e._i=[],e.init=function(i,s,a){function g(t,e){var o=e.split(".");2==o.length&&(t=t[o[0]],e=o[1]),t[e]=function(){t.push([e].concat(Array.prototype.slice.call(arguments,0)))}}p||((p=t.createElement("script")).type="text/javascript",p.crossOrigin="anonymous",p.async=!0,p.src=s.api_host.replace(".i.posthog.com","-assets.i.posthog.com")+"/static/array.js",p.onerror=function(){p=null},(r=t.getElementsByTagName("script")[0]).parentNode.insertBefore(p,r));var u=e;for(void 0!==a?u=e[a]=[]:a="posthog",u.people=u.people||[],Object.defineProperty(u,"toString",{configurable:!0,enumerable:!0,writable:!0,value:function(t){var e="posthog";return"posthog"!==a&&(e+="."+a),t||(e+=" (stub)"),e}}),Object.defineProperty(u.people,"toString",{configurable:!0,enumerable:!0,writable:!0,value:function(){return u.toString(1)+".people (stub)"}}),o="Eh Rh Ph Oh Dh qh init yd bd dd fd Sd wc md xd ad Ed Ad Rd Pd Od Fd capture getExtension gr pd gd calculateEventProperties Dd register register_once register_for_session unregister unregister_for_session zd hd jd getFeatureFlag getFeatureFlagPayload getFeatureFlagResult getAllFeatureFlags isFeatureEnabled reloadFeatureFlags updateFlags updateEarlyAccessFeatureEnrollment getEarlyAccessFeatures on onFeatureFlags onSurveysLoaded onSessionId getSurveys getActiveMatchingSurveys onActiveMatchingSurveysChanged renderSurvey displaySurvey cancelPendingSurvey canRenderSurvey canRenderSurveyAsync Hd identify setPersonProperties unsetPersonProperties group resetGroups setPersonPropertiesForFlags resetPersonPropertiesForFlags setGroupPropertiesForFlags resetGroupPropertiesForFlags reset Ud shutdown setIdentity clearIdentity get_distinct_id getGroups get_session_id get_session_replay_url alias set_config startSessionRecording stopSessionRecording sessionRecordingStarted captureException addExceptionStep captureLog startExceptionAutocapture stopExceptionAutocapture loadToolbar get_property getSessionProperty qd Td createPersonProfile setInternalOrTestUser Bd Uh zh opt_in_capturing opt_out_capturing Hh has_opted_in_capturing has_opted_out_capturing get_explicit_consent_status is_capturing clear_opt_in_out_capturing $d debug kc Ds getPageViewId captureTraceFeedback captureTraceMetric rd".split(" "),n=0;n<o.length;n++)g(u,o[n]);e._i.push([i,s,a])},e.__SV=1)}(document,window.posthog||[]);
    posthog.init('phc_mVCCmKX2rW8xRqouWmtvAYnVTXRHwNQmpVQRzfXwoThh', {
        api_host: 'https://us.i.posthog.com',
        defaults: '2026-05-30',
        person_profiles: 'identified_only', // or 'always' to create profiles for anonymous users as well
    })
</script>
<script>
if (window.goatcounter === undefined) {
  window.goatcounter = {};
}
if (location.hostname === "bernsteinbear.com") {
  window.goatcounter.endpoint = 'https://tekknolagi.goatcounter.com/count';
} else {
  window.goatcounter = {
      endpoint: 'https://no.goatcounter.com/count',
  };
  alert("Please remove all mentions of me (socials, Google Analytics, GoatCounter, ...) from your fork of my website. Thanks!");
}
</script>
<script async src="/assets/js/count.js"></script>
{% endif %}
