import argparse
import json
import os
import urllib.error
import xml.etree.ElementTree as ET
from urllib.parse import urlencode
from urllib.request import urlopen, urlretrieve


API = "https://mastodon.social/api/v1"
DB = "mastodon.json"
PAGE = "microblog.html"
PERMALINK = "/microblog/"
USER = "tekknolagi"
MASTODON_MEDIA = "assets/mastodon"


def open_db():
    try:
        with open(DB, "r") as f:
            return json.load(f)
    except FileNotFoundError:
        return {}


def save_db(data):
    with open(DB, "w+") as f:
        json.dump(data, f, indent=2)


def api_url(path: str, params: dict = None):
    if params is None:
        params = {}
    query = urlencode(params)
    return f"{API}/{path}?{query}"


def lookup_user(username):
    response = urlopen(api_url("/accounts/lookup", {"acct": username}))
    return json.load(response)


def get_toots(user_id, params=None):
    response = urlopen(api_url(f"/accounts/{user_id}/statuses", params))
    json_response = json.load(response)
    result = {}
    for post in json_response:
        del post["account"]
        result[post["id"]] = post.copy()
    return result


def get_all_toots(user_id, min_id):
    while True:
        print(f"Fetching toots after {min_id}...")
        batch = get_toots(user_id, {"min_id": min_id, "limit": 40})
        if not batch:
            break
        min_id = max(int(id) for id in batch.keys())
        yield batch


def update_db(db):
    if "user" not in db:
        db["user"] = lookup_user(USER)
    user_id = db["user"]["id"]
    if "toots" in db:
        min_id = max(int(id) for id in db["toots"].keys())
    else:
        db["toots"] = {}
        min_id = 0
    for batch in get_all_toots(user_id, min_id):
        db["toots"].update(batch)
        save_db(db)


def should_display(post):
    if post["in_reply_to_id"]:
        return False
    if post["reblog"]:
        return False
    return True


def rewrite_media_filename(media_url, media_id):
    _, media_ext = os.path.splitext(media_url)
    return f"{MASTODON_MEDIA}/{media_id}{media_ext}"


def render_post(post):
    builder = ET.TreeBuilder()
    li = builder.start("blockquote", {"class": "post"})
    try:
        # Make a wrapper root element or ET will complain
        li.append(ET.fromstring("<span>" + post["content"] + "</span>"))
    except ET.ParseError as e:
        print(f"Error parsing content for post {post['id']}: {e}")
        print(post["content"])
        raise
    for media in post["media_attachments"]:
        if media["type"] == "image":
            height = str(media["meta"]["small"]["height"])
            width = str(media["meta"]["small"]["width"])
            media_filename = rewrite_media_filename(media["url"], media["id"])
            media_url = f"/{media_filename}"
            media_desc = media.get("description", None)
            attrs = {"src": media_url, "height": height, "width": width}
            if media_desc:
                attrs["alt"] = media_desc
            builder.start("img", attrs)
            builder.end("img")
        else:
            # TODO(max): Figure out how (if?) we want to embed other media
            pass
    builder.start("br", {})
    builder.end("br")
    builder.start("span", {"class": "time"})
    builder.start("a", {"href": post["url"]})
    builder.data(post["created_at"])
    builder.end("a")
    builder.end("span")
    builder.end("blockquote")
    return ET.tostring(builder.close(), encoding="unicode")


def render(db):
    newline = "\n"
    newest_first = sorted(
        db["toots"].values(), key=lambda x: int(x["id"]), reverse=True
    )
    return f"""---
title: Microblog
layout: page
permalink: {PERMALINK}
---
<style>
blockquote.post {{
  display: inline-block;
  font-family: "Helvetica Neue", Roboto, "Segoe UI", Calibri, sans-serif;
  font-size: 12px;
  font-weight: bold;
  line-height: 16px;
  border-color: #eee #ddd #bbb;
  border-radius: 5px;
  border-style: solid;
  border-width: 1px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.15);
  margin: 10px 5px;
  padding: 0 16px 16px 16px;
  width: 468px;
}}

blockquote.post img {{
  display: block;
  max-width: 100%;
  width: auto;
  height: auto;
}}

blockquote.post p {{
  font-size: 16px;
  font-weight: normal;
  line-height: 20px;
}}

blockquote.post a {{
  color: inherit;
  font-weight: normal;
  outline: 0 none;
}}

blockquote.post .time a {{
  text-decoration: none;
}}

blockquote.post a:hover,
blockquote.post a:focus {{
  text-decoration: underline;
}}

/* Thanks to https://codepen.io/miyano/pen/oMREdZ */
.mstdn{{
  display:inline-block;
  background-color:#282c37;
  color:#d9e1e8;
  text-decoration:none;
  padding:4px 10px 4px 30px;
  border-radius:4px;
  font-size:16px;
  background-image: url('data:image/svg+xml;charset=utf8,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20width%3D%2261.076954mm%22%20height%3D%2265.47831mm%22%20viewBox%3D%220%200%20216.4144%20232.00976%22%3E%3Cpath%20d%3D%22M211.80734%20139.0875c-3.18125%2016.36625-28.4925%2034.2775-57.5625%2037.74875-15.15875%201.80875-30.08375%203.47125-45.99875%202.74125-26.0275-1.1925-46.565-6.2125-46.565-6.2125%200%202.53375.15625%204.94625.46875%207.2025%203.38375%2025.68625%2025.47%2027.225%2046.39125%2027.9425%2021.11625.7225%2039.91875-5.20625%2039.91875-5.20625l.8675%2019.09s-14.77%207.93125-41.08125%209.39c-14.50875.7975-32.52375-.365-53.50625-5.91875C9.23234%20213.82%201.40609%20165.31125.20859%20116.09125c-.365-14.61375-.14-28.39375-.14-39.91875%200-50.33%2032.97625-65.0825%2032.97625-65.0825C49.67234%203.45375%2078.20359.2425%20107.86484%200h.72875c29.66125.2425%2058.21125%203.45375%2074.8375%2011.09%200%200%2032.975%2014.7525%2032.975%2065.0825%200%200%20.41375%2037.13375-4.59875%2062.915%22%20fill%3D%22%233088d4%22%2F%3E%3Cpath%20d%3D%22M177.50984%2080.077v60.94125h-24.14375v-59.15c0-12.46875-5.24625-18.7975-15.74-18.7975-11.6025%200-17.4175%207.5075-17.4175%2022.3525v32.37625H96.20734V85.42325c0-14.845-5.81625-22.3525-17.41875-22.3525-10.49375%200-15.74%206.32875-15.74%2018.7975v59.15H38.90484V80.077c0-12.455%203.17125-22.3525%209.54125-29.675%206.56875-7.3225%2015.17125-11.07625%2025.85-11.07625%2012.355%200%2021.71125%204.74875%2027.8975%2014.2475l6.01375%2010.08125%206.015-10.08125c6.185-9.49875%2015.54125-14.2475%2027.8975-14.2475%2010.6775%200%2019.28%203.75375%2025.85%2011.07625%206.36875%207.3225%209.54%2017.22%209.54%2029.675%22%20fill%3D%22%23fff%22%2F%3E%3C%2Fsvg%3E');
  background-size:16px;
  background-repeat:no-repeat;
  background-position:top 50% left 8px;
  transition:all 0.5s;
}}
.mstdn > span{{
  color:#9baec8;
  font-size:12px;
  padding-left:3px;
}}
.mstdn > span:before{{
  content:"@";
}}
</style>
<div><a href="{{{{ site.mastodon.url }}}}" class="mstdn">{{{{site.mastodon.username}}}}</a></div>
{newline.join(render_post(post) for post in newest_first if should_display(post))}
"""


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--download-media", action="store_true")
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()
    db = open_db()
    if args.update:
        try:
            update_db(db)
        except urllib.error.HTTPError as e:
            if e.code == 429:
                print("Rate limited. Try again later.")
            else:
                raise
    if args.download_media:
        print("Downloading media...")
        os.makedirs(MASTODON_MEDIA, exist_ok=True)
        for post in db["toots"].values():
            for media in post["media_attachments"]:
                media_id = media["id"]
                media_url = media["url"]
                media_filename = rewrite_media_filename(media_url, media_id)
                if not os.path.exists(media_filename):
                    urlretrieve(media_url, media_filename)
    text = render(db)
    with open(PAGE, "w+") as f:
        f.write(text)
