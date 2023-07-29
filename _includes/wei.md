<div id="mySizeChartModal" class="ebcf_modal">
  <div class="ebcf_modal-content">
    <span class="ebcf_close">&times;</span>
    <p>Some text in the Modal..</p>
  </div>
</div>

<script>
// Get the modal
var ebModal = document.getElementById('mySizeChartModal');

// Get the button that opens the modal
var ebBtn = document.getElementById("mySizeChart");

// Get the <span> element that closes the modal
var ebSpan = document.getElementsByClassName("ebcf_close")[0];

// // When the user clicks the button, open the modal
// ebBtn.onclick = function() {
//     ebModal.style.display = "block";
// }

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
    document.querySelector('body').innerHTML='<h1>Your browser contains Google DRM</h1>"Web Environment Integrity" is a Google euphemism for a DRM that is designed to prevent ad-blocking. In support of an open web, this website does not function with this DRM. Please install a browser such as <a href="https://www.mozilla.org/en-US/firefox/new/">Firefox</a> that respects your freedom and supports ad blockers.';
}
</script>
