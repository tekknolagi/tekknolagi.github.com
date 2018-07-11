---
title: "JumboSmash: a technical retrospective"
layout: post
date: 2018-07-12 10:08:20 PDT
---

### Intro

This past year, I had the honor and pleasure to work on the JumboSmash app with

* [Winnona DeSombre](https://winnona.github.io/)
* [Shanshan Duan](https://shanshanduan.info/)
* [Chris Gregory](http://chrisgregory.me/)
* [Zach Kirsch](http://zachkirsch.com/)
* [Emily Lin](http://emilyjlin.com/)
* [Yuki Zaninovich](https://yzan424.github.io/).

For those who were not in and around[^trivia] Tufts while the app was active,
JumboSmash can best be described as "the senior final get-together app before
everyone departs". You know, to rekindle past friendships and enable people who
never crossed paths to meet one another... oh, who are we kidding --- it's the
senior hook-up app.

[^trivia]: I heard that JumboSmash made it into a question from trivia night at
      PJ Ryan's. Wild.

In previous years, JumboSmash has had a wide range of incarnations. In its
first year, it was a web form that people could fill out, and somebody made
matches by hand. Then it launched the following year as a website with similar
functionality. Last year, for Spring 2017, it was re-made as a Tinder-style
mobile app (iOS and Android). Regardless of whether it makes much practical
sense[^raze-it], a different team creates the JumboSmash product anew every
year.

[^raze-it]: People are divided on this. At some level, creating an app from the
      ground up increases the sense of ownership and pride when it succeeds. It
      also holds the creators back, because much of the work is re-creating a
      foundation instead of focusing on new and exciting developments. I got my
      ear talked off by both parties.

This year, we decided to keep the Tinder functionality (swiping, matching,
chatting with matches) and add on some other fun features:

* Reacting on other people's profiles, as on Facebook and Slack
* Adding tags to the bio, including some riotous tags by Sophie Lehrenbaum
* Senior bucket list items
* A "senior goal" as part of the user bio
* {some other stuff}
* Pre-setup blocking
* Bans

{some other stuff about what we developed and how}

### Massive scaling problems

We set a launch date in May, the night of a major senior party. I opted to stay
home "on call" because I wasn't feeling particularly party that evening. I sat
there with a console open, ready to watch the floodgates open and for people to
start swiping. At 11:59pm, that is precisely what happened.

At first, things went okay. The server allowed roughly three hundred (!) people
to sign up rapidly before it began noticeably falling over. The logs looked
fine --- I learned that the server was falling over from two sources:

1. Heroku's load monitoring page told me there was a large and increasing
   amount of API timeouts
2. My email and Facebook messenger inboxes began to blow up from people
   complaining

I quickly notified the rest of the team, who promptly left the party and
started toward Computer Science building. It was about 1am at this point. They
arrived (uh, "extremely tired" from partying) and we got to work.

The first order of business was to increase the resources available on Heroku
as a temporary stopgap. We increased the number of web workers from 10 to 50
and bumped up the database to allow more concurrent connections. As we found
out, this barely eased the stress on our servers.

### Profiling the slow endpoints

Winnona and Chris used regular expressions to make short work of the server
logs, making a sorted list of the endpoints that took the most time to produce
results. As it turned out, `/users/swipable` and `/users/me` both took
incredible amounts of time to do anything.

To add more detail to our analysis, I wrapped all of the routes with an
`inspect` function wrapper:

```python
def inspect(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        endpoint = request.url_rule.endpoint
        start = get_current_time()
        global reqid
        reqid += 1
        print "BEGINNING REQUEST FOR {} ID {} AT ({})".format(
            endpoint, reqid, start
        )
        res = f(*args, **kwargs)
        end = get_current_time()
        print "ENDING REQUEST FOR {} ID {} AT ({}) DELTA ({})".format(
            endpoint, reqid, end, end-start
        )
        return res
    return decorated_function
```

This allowed us to customize the information that we logged in order to better
understand what was happening. It also allowed us to quickly eyeball slow
routes by doing something like `heroku logs --tail | grep DELTA` to see what
was taking a long time without any extraneous request information.

As we found out , requests that time out do not even make it into the Heroku
logs, because it wouldn't make sense to add an "elapsed time" field to the
request --- but they are still included in our logs. So this additional level
of logging turned up routes that *didn't even show up* in the Heroku logs
because they were timing out. `/users`, for example, frequently took upwards of
*thirty seconds* to execute. Absolutely inexcusable, particularly at this
scale.

At this point it was roughly 2am, and we made the executive decision to take
down the server until we could fix the problems with any degree of certainty.

Later, after much investigation and consternation, we went to bed. It was 5am.

### Database schema

At this juncture I should probably explain part of our database layout, since
it's pertinent to our performance investigation. As expected, we have a `users`
table (output of this and following tables truncated for ease of viewing):

```
max@localhost:jumbosmash> describe users;
+-----------------------+--------------------------+
| Column                | Type                     |
|-----------------------+--------------------------|
| id                    | integer                  |
| email                 | character varying(120)   |
| preferred_name        | character varying(120)   |
| session_key           | character varying(120)   |
| verified              | boolean                  |
| verification_code     | character varying(64)    |
| accepted_coc          | boolean                  |
| bio                   | text                     |
| created_at            | timestamp with time zone |
| uuid                  | uuid                     |
| firebase_uid          | character varying(120)   |
| firebase_notificat... | text                     |
| surname               | character varying(120)   |
| major                 | character varying(240)   |
| class_year            | integer                  |
| active                | boolean                  |
| senior_goal           | text                     |
+-----------------------+--------------------------+
```

and also a `matches` table:

```
max@localhost:jumbosmash> describe matches;
+-------------------+--------------------------+
| Column            | Type                     |
|-------------------+--------------------------|
| id                | integer                  |
| created_at        | timestamp with time zone |
| conversation_uuid | uuid                     |
| unmatched         | boolean                  |
| updated_at        | timestamp with time zone |
+-------------------+--------------------------+
```

and a `users_matches` table:

```
max@localhost:jumbosmash> describe users_matches;
+----------+---------+
| Column   | Type    |
|----------+---------|
| id       | integer |
| user_id  | integer |
| match_id | integer |
+----------+---------+
```

Notice that the `matches` table doesn't have two columns like `user_a` and
`user_b` to contain the two people contained in a match. We wanted matches to
be extensible to N people, where N was, say, three. This initially complicated
matters, but in the end was reasonably easily optimized.

### Fixing the slow endpoints

#### Queries on queries on queries

The first order of business was fixing `/users/swipable`, since it was by and
away the most oft-used slow route. We opened up a text editor, looked at the
code for the route, and collectively groaned. The code looked something like
this ("makes DB query" annotations added for this post):

```python
@app.route('/users/swipable', methods=['GET'])
@require_authentication
@inspect
def users_get_swipable():
    users = User.query.all()  # makes DB query
    my_blocks = Block.query...  # makes DB query
    blocked_me = Block.query...  # makes DB query
    active_matches = me.matches  # makes a couple DB queries

    for user in users:
        some_sanity_checks(user)
        swipe = most_recent_swipe_between(me, user)  # makes DB query
        some_sanity_checks(swipe)  # makes a couple DB queries

    # ...
```

Astute readers will notice that this makes *quite a few* database queries. In
fact, the number of database queries scales linearly with the number of users
on JumboSmash. This is, uh, bad. Consider:

* 300 (at the time) concurrent users
* a simple DB query takes on the order of 30ms

You do the math. We did and it wasn't pretty.

Perhaps the worst part of this, though, is that this entire route could be
transformed into a handful of medium-complicated SQL queries. We only didn't do
that up front because we were unfamiliar with the ORM library, SQLAlchemy, and
unwilling to have one endpoint be hand-written SQL. And, of course, we forgot
that this was the case before releasing. Since we didn't have realistically big
test data or any automated tests for the API, we completely failed to notice
that this route performed terribly. If latent performance issues constitute
technical debt, this was technical poverty.

So, as happens in all my stories, we called in the big guns: Tom and Logan.
Bless them. Tom and I got to work transforming the fustercluck of an endpoint
into one large SQLAlchemy expression that resulted in a reasonably fast SQL
query. After some time I left him to it to analyze the rest of the codebase.

As we found out later on in the process, the resulting SQLAlchemy concoction
was *heinous*, coming in at 55 non-blank lines. Thankfully, it only took one
other all-nighter with Logan much later on to tame it to something "reasonable"
like 48 non-blank lines (including some comments).

#### `/users` makes at least one query per user

Turns out, we made a similar mistake in `/users`. Upon inspection, the mistake
is painfully obvious. It's funny how such a short bit of code can be so
devastating.

```python
@app.route('/users', methods=['GET'])
@require_authentication
@inspect
def users():
    """List all of the users
    Request params: None
    (Success) Response params: { users : LIST<USER> }, 200
    For each possible error response:
        None
    """
    users = User.query.all()  # makes a DB query
    serialized_users = serialize(users)  # makes a couple DB queries per user
    for serialized_user in serialized_users:
        serialized_user['my_reacts'] = \
            serialize(UserReact.query  # makes a DB query
                      .filter(UserReact.user_on_id == serialized_user['id'])
                      .filter(UserReact.user_from_id == g.u.id).all())
    return jsonify(users=serialized_users)
```

Ignore for a minute the opaque `serialize` function. Take a look at the rest of
the endpoint. For every user, it queries the `react`s the current user has made
on their profile. That's pretty heinous, and also easily avoidable with SQL.
Our hot-patch fix, though, was to more or less do the query in Python:

```python
@app.route('/users', methods=['GET'])
@require_authentication
@inspect
def users():
    # ...
    users = User.query.all()  # makes a DB query
    user_reacts = (UserReact.query  # makes a DB query
                   .filter(UserReact.user_from_id == g.u.id).all())

    serialized = []
    for user in users:
        relevant_reacts = [ur for ur in user_reacts
                           if ur.user_on_id == user['id']]
        suser = serialize(user)  # makes a DB query
        suser['my_reacts'] = serialize(relevant_reacts)
        serialized.append(suser)

    return jsonify(users=serialized)
```

This takes the number of queries (again, ignoring `serialize`) to two. Total.

#### Serialize all the things

Okay, so let's take a look at `serialize`. My idea with `serialize` was to
write a function that can turn any ORM object or Python object into JSON that
can be returned to the front-end. So for example, you can do
`serialize(a_user)` or `serialize(a_swipe)` or `serialize(an_int)`. I
originally had planned to make each object have a `.serialize()` method and
recursively serialize sub-objects that way, but I encountered some sort of
barrier that I no longer remember and decided to go in this direction instead.
The entire function is reproduced below in all its glory:

```python
def serialize(o, additional_fields=None):
    additional_fields = additional_fields or []
    t = type(o)
    jsontypes = (int, long, float, str, unicode, bool, type(None))
    prims = (uuid.uuid4, SwipeDirection, TextType)
    if t in prims:
        return str(o)
    elif t is datetime.datetime:
        if o.tzinfo:
            return o.isoformat()
        else:
            return eastern.localize(o).isoformat()
    elif t in jsontypes:
        return o
    elif t is list or t is InstrumentedList:
        return [serialize(x) for x in o]
    elif t is dict or t is InstrumentedDict:
        return dict([(k, serialize(v)) for k, v in o.iteritems()])
    # sqlalchemy.util._collections.result
    elif callable(getattr(o, '_asdict', None)):
        return serialize(o._asdict())
    elif issubclass(t, BaseQuery):
        return serialize(o.all())
    else:
        fields = (o.__safe_fields__ or []) + additional_fields
        # Some fields might be NULL
        return dict([(field, serialize(getattr(o, field)))
                     for field in fields])
```

Aside from being a tangled conditional mess, it uses all sorts of questionable
features and attributes of our chosen ORM library without explanation. Like
`elif callable(getattr(o, '_asdict', None)):`. What the hell is that about? If
I recall correctly, there's a particular type of SQLAlchemy result that can be
turned into a dict but it's not an InstrumentedDict. I don't know. I should
have explained that in the code, probably.

Readability aside, it has some subtle performance implications. Consider, for
example, that there are some fields on objects that shouldn't be serialized. On
user objects, the world probably shouldn't know about `firebase_uid`. That's
sensitive information. In fact, the world probably shouldn't know about most of
the fields on the user object, so we only ever want to serialize the so called
"safe fields" of a database object (in general). So we decided to add a field
called `__safe_fields__` to every class that represents a table in the
database. For `User`, this looks like this:

```python
class User(MyBase, db.Model):
    __tablename__ = 'users'
    __safe_fields__ = ['id', 'uuid', 'email', 'preferred_name', 'verified',
                       'accepted_coc', 'bio', 'images', 'firebase_uid',
                       'surname', 'full_name', 'major', 'profile_reacts',
                       'tags', 'class_year', 'events', 'senior_goal']
    # ...
```

(If the programmer would like to see more fields in a response from
`serialize`, the field names must be present in the `additional_fields`
parameter.)

The astute reader will notice that some of the safe fields are not in the
database schema, like `images`, `full_name`, or `profile_reacts`. That's
because some are implemented as SQLAlchemy relationships (glorified JOINs).
Others are implemented as [Python properties][python-properties], and therefore
handled in the last case of `serialize` with `getattr`. Except they're not
normal field accesses: they're function calls. Every access of `full_name`
formats the full name from the `preferred_name` and `surname` constituent
parts. Every access of `profile_reacts` makes a database query.  Perhaps you
can see where this is going...

[python-properties]: http://stackabuse.com/python-properties/

A call to `serialize(some_user)` makes the following queries:

1. Fetch the `images` from the `user_images` table
1. Fetch the `react`s to this user's profile via the `users_reacts` table
1. Fetch the `tag`s on this user's profile via the `users_tags` table
1. Fetch the `event`s this user is attending via the `event_responses` table

This is all pretty inexcusable because it's still possible to smush this all
into one large SQL query instead. But here we are.

Now consider what happens when a `Match` is serialized, for example in the
event that `/users/me` is requested and the endpoint has to display all of the
user's `active_matches`:

1. Serialize the user, including all the fetches above
1. Additionally, fetch all of the user's `match`es via the `users_matches`
   table and serialize them
   1. For each serialized `match`, since `users` is considered a "safe field",
      of a `match`, every user present in the match is re-fetched and
      re-reserialized (including all the fetches above)
1. For each blocked `user`, re-fetch and re-serialize
1. For each `user` who `react`ed on their profile, re-fetch and re-serialize

This is, in technical terms, rather slow. And, if you consider the fact that
JSON isn't the world's most information-dense serialization format, the
responses blew up to be on the orders of megabytes. *Megabytes*. This really
does not deliver the sort of "swipe fast and match people" experience that we
were trying to bring to campus.

After a close inspection of the code for the mobile app, we did our best to
pare down the data in the API response to the bare minimum required by the app
to function.[^postels-law] In some cases, this meant limiting "user objects"
instead to small blobs like `{"id": 7}` because really, the client only needed
the ID.

[^postels-law]: (As learned in COMP 117 IDS) [Postel's law][postels-law], also
    known as the robustness principle, recommends that protocols be
    conservative in what they send and liberal in what they accept. I
    theoretically knew this at the time of developing the app (I was
    concurrently enrolled in the class), but it did not really click until we
    were stuck supporting two versions of the mobile app with one API because
    we did not want to force everybody to update. Massive shout-out to Noah
    Mendelsohn.

[postels-law]: https://en.wikipedia.org/wiki/Robustness_principle

#### Cache that shit when we can't change due to backward compatibility

We deemed some of the flaws of `/users` too annoying, time-consuming, or
unavoidable to change completely. We discovered slowly that the mobile app used
that route as the primary source of truth for displaying user information, so
we couldn't neuter it too much. And our primary purpose was to get people
swiping and matching, not reach engineering perfection. As such, we chose a
quick and dirty solution: stuff the response in a Redis cache with a five
minute TTL.

Anyone who writes code for a living, or even as a hobby, or even who wrote
50 lines of code ten years ago will have just gasped at that solution. But
it worked.

#### Stop requesting `/users` so much and return relevant information elsewhere

While having the old version of the mobile app in the wild meant that we
couldn't change `/users` too much, we could encourage people to use the new
version. And the new version could definitely update its offline store of user
information from other places, thereby reducing overall load on `/users`.

We squeaked in more information everywhere we could without breaking API
compatibility: in the notification payloads, in `/users/swipable`, in new
match notifications, etc. This strategy really helped take some load off of the
extraordinarily expensive routes.

### Logging on the front-end

Readers of this post at this point will probably be asking, "what do you mean,
the new version of the app?" Well. As it turns out, the problems weren't *all*
to do with the server.

One of the major client-side problems involved logging every request to the
server and its corresponding response to the console. This was an excellent
idea in development, because it enabled us to quickly debug both requests and
responses. This was not such an idea in production, because logging is fast on
the simulator and slow on real devices. Which is funny, because there were *no
real consoles* on our users' devices.

One of the major client-side speedups came from only logging if the app was in
development mode. Aside from that, the new version tried its best to help
reduce server load using the method described in the section above.

### Learning to say 'fuck it'

Look, learning technical things about problem solving and API design is great,
but it's not the most important thing. We developed a free app over the course
of the year, occasionally each pouring upwards of 20 hours per week into it. We
did this with no compensation or expectation of a reward. We did this *for our
classmates*. And maybe for the occasional high five.

And yet, the complaints rolled in. Sarcastic, acidic, biting complaints. We got
them by email, by Facebook Messenger, as app reviews, and even by text. Somehow
our cell phone numbers got around. This was extraordinarily disheartening for
everybody on the team. We had to set up a rotating schedule to respond to
emails because it was too soul-crushing for anybody to do for an extended
period of time.

At some point we all got together and decided to just stop responding. Who
cares if someone has duplicate pictures in their profile only sometimes because
some kind of obscure network race condition? Not us. It was senior week, and,
being seniors, we deserved to enjoy it at least a little. So we sat back, had a
beer or three, and enjoyed one another's company.

### Uh, some kind of conclusion?

{hmm}


<br />
<hr style="width: 100px;" />
<!-- Footnotes -->
