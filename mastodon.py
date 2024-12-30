import argparse
import json
import urllib.error
from urllib.parse import urlencode
from urllib.request import urlopen


API = "https://mastodon.social/api/v1"
DB = "mastodon.json"
PAGE = "microblog.html"
USER = "tekknolagi"


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


def render_post(post):
    return f"""<li>{post["content"]} <small><a href="{post["url"]}">(link)</a></small></li>"""


def render(db):
    newline = "\n"
    newest_first = sorted(
        db["toots"].values(), key=lambda x: int(x["id"]), reverse=True
    )
    return f"""---
title: Microblog
layout: page
---
<ul>
{newline.join(render_post(post) for post in newest_first if should_display(post))}
</ul>
"""


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
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
    text = render(db)
    with open(PAGE, "w+") as f:
        f.write(text)
