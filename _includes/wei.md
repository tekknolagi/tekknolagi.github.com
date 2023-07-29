<style>
/* The Modal (background) */
.ebcf_modal {
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
.ebcf_modal-content {
    background-color: #fefefe;
    margin: auto;
    padding: 20px;
    border: 1px solid #888;
    width: 50%;
}

/* The Close Button */
.ebcf_close {
    color: #aaaaaa;
    float: right;
    font-size: 28px;
    font-weight: bold;
}

.ebcf_close:hover,
.ebcf_close:focus {
    color: #000;
    text-decoration: none;
    cursor: pointer;
}
</style>

<div id="mySizeChartModal" class="ebcf_modal">
  <div class="ebcf_modal-content">
    <span class="ebcf_close">&times;</span>
    <h1>Your browser contains Google DRM</h1><q>Web Environment Integrity</q>
    is a Google euphemism for a DRM that is designed to prevent ad-blocking. In
    support of an open web, this website <s>does not function with this DRM</s>
    is a little bit irritating. Please install a browser such as <a
    href="https://www.mozilla.org/en-US/firefox/new/">Firefox</a> that respects
    your freedom and supports ad blockers.
  </div>
</div>

<script>
// Get the modal
var ebModal = document.getElementById('mySizeChartModal');

// Get the <span> element that closes the modal
var ebSpan = document.getElementsByClassName("ebcf_close")[0];

// When the user clicks on <span> (x), close the modal
ebSpan.onclick = function() {
    ebModal.style.display = "none";
}

// When the user clicks anywhere outside of the modal, close it
window.onclick = function(event) {
    if (event.target == ebModal) {
        ebModal.style.display = "none";
    }
}

/*TODO(max): Flip conditional*/
if(!(navigator.getEnvironmentIntegrity!==undefined)) {
    // Display the modal
    ebModal.style.display = "block";
}
</script>
