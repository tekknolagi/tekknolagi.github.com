const relay = "wss://offchain.pub";
const socket = new WebSocket(relay);
const pubkey =
  "ff6560d3d0c180d8be922541650ca53debdfa50d10d30d05e1320c0a04b64584";

function subscribe(pubkey) {
  const filter = {
    authors: [pubkey],
  };
  const subscription = ["REQ", "my-sub", filter];
  const subscription_json = JSON.stringify(subscription);
  // sessionStorage.subscription = subscription;
  //socket.send(sessionStorage.subscription);
  socket.send(subscription_json);
}

function escapeHTML(unsafeText) {
  // https://stackoverflow.com/a/48054293/569183
  const div = document.createElement("div");
  div.innerText = unsafeText;
  return div.innerHTML;
}

socket.addEventListener("open", function (event) {
  console.log("open", event);
  subscribe(pubkey);
});

socket.addEventListener("message", function (event) {
  const message_data_json = event.data;
  const message_data = JSON.parse(message_data_json);
  if (!(message_data instanceof Array)) {
    console.log("Invalid message: expected Array", message_data);
    return;
  }
  if (message_data.length < 2) {
    console.log("Invalid message: too short", message_data);
    return;
  }
  const message_type = message_data[0];
  if (message_type !== "EVENT") {
    // Can ignore EOSE... for now? TODO: do we need to re-open a sub?
    return;
  }
  if (message_data.length !== 3) {
    console.log("Invalid EVENT message: too short", message_data);
    return;
  }
  const event_data = message_data[2];
  if (event_data.kind !== 1) {
    // Ignore non text-note kinds for now
    return;
  }
  const text = event_data.content;
  const date = new Date(event_data.created_at * 1000);
  const date_str = date.toLocaleDateString("en-US");
  const time_str = date.toLocaleTimeString("en-US");
  const li = document.createElement("li");
  li.innerText = `${escapeHTML(text)}\n  ${time_str} on ${date_str}`;
  document.querySelector("#nostr-container ol").appendChild(li);
});
