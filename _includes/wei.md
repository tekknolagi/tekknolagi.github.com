<!-- Thanks to colbyalbo for the HTML/CSS/JS: https://codepen.io/colbyalbo/pen/gRogbE -->
<style>
/* The Modal (background) */
.wei_modal {
    display: none; /* Hidden by default */
    position: fixed; /* Stay in place */
    z-index: 1; /* Sit on top */
    padding-top: 100px; /* Location of the box */
    left: 0;
    top: 0;
    width: 100%; /* Full width */
    height: 100%; /* Full height */
    overflow: auto; /* Enable scroll if needed */
    background-color: rgb(0,0,0); /* Fallback color */
    background-color: rgba(0,0,0,0.4); /* Black w/ opacity */
}

/* Modal Content */
.wei_modal-content {
    background-color: #fefefe;
    margin: auto;
    padding: 20px;
    border: 1px solid #888;
    width: 50%;
}

/* The Close Button */
.wei_close {
    color: #aaaaaa;
    float: right;
    font-size: 28px;
    font-weight: bold;
}

.wei_close:hover,
.wei_close:focus {
    color: #000;
    text-decoration: none;
    cursor: pointer;
}
</style>

<div id="weiModal" class="wei_modal">
  <div class="wei_modal-content">
    <span class="wei_close">&times;</span>
    <h1>Your browser contains Google DRM</h1>

    <p>This is a bad thing and is yet another attempt by Google to unilaterally
    change web standards.</p>

    <p><q>Web Environment Integrity</q> is a Google euphemism for a DRM that is
    designed to prevent ad-blocking. In support of an open web, this website
    <s>does not function with this DRM</s> is a little bit irritating. Please
    install a browser such as <a
    href="https://www.mozilla.org/en-US/firefox/new/">Firefox</a> that respects
    your freedom and supports ad blockers.</p>
  </div>
</div>

<script>
// Get the modal
var weiModal = document.getElementById('weiModal');

// Get the <span> element that closes the modal
var weiSpan = document.getElementsByClassName("wei_close")[0];

// When the user clicks on <span> (x), close the modal
weiSpan.onclick = function() {
    weiModal.style.display = "none";
}

// When the user clicks anywhere outside of the modal, close it
window.onclick = function(event) {
    if (event.target == weiModal) {
        weiModal.style.display = "none";
    }
}

/*TODO(max): Flip conditional*/
if(!(navigator.getEnvironmentIntegrity!==undefined)) {
    // Display the modal
    weiModal.style.display = "block";
}
</script>
