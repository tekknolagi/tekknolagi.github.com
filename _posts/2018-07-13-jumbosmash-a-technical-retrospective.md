---
title: "JumboSmash: a technical retrospective"
layout: post
date: 2018-07-13 14:39:00 PDT
co_authors: Zach Kirsch
---

### Intro

This past year, I had the honor and pleasure to work on the JumboSmash app with

* [Winnona DeSombre](https://winnona.github.io/)
* Shanshan Duan
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
first year, it was a web form that people could fill out, and someone or
something matched people up. Then it launched the following year as a website
with similar functionality. Last year, for Spring 2017, it was re-made as a
Tinder-style mobile app (iOS and Android). Regardless of whether it makes much
practical sense[^raze-it], a different team creates the JumboSmash product anew
every year.

[^raze-it]: People are divided on this. At some level, creating an app from the
      ground up increases the sense of ownership and pride when it succeeds. It
      also holds the creators back, because much of the work is re-creating a
      foundation instead of focusing on new and exciting developments. I got my
      ear talked off by both parties.

      This is likely the first year that it would have made any sense to re-use
      something from the past year's JumboSmash project. Even if we wanted to,
      though, last year's team forbade us from using any of it.

This year, we decided to keep the Tinder functionality (swiping, matching,
chatting with matches) and add on some other fun features:

* Reacting on other people's profiles, as on Facebook and Slack
* Adding tags to the bio, including some riotous tags by Sophie Lehrenbaum
* Senior bucket list items
* A "senior goal" as part of the user bio ("I want to eat pizza on the roof of
  the English building")
* The schedule for senior week and allowing users to display which events they
  were going to, and see who else was attending
* GIF support in chat
* Allowing users to pre-emptively block people they know they won't want to
  see, even if the other person has not signed up for JumboSmash yet
* Admin bans for users that do bad things
* LDAP support for log-in
* Pre-registration for people with senior standing that weren't listed as such
  in LDAP (e.g. people taking a 5th year)

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
1. My email and Facebook messenger inboxes began to blow up from people
   complaining

I quickly notified the rest of the team, who promptly left the party and
started toward the Computer Science building. It was about 1am at this point.
They arrived (uh, "extremely tired" from partying) and we got to work.

The first order of business was to increase the resources available on Heroku
as a temporary stopgap. We increased the number of web workers from 10 to 50
and bumped up the database to allow more concurrent connections. As we found
out, this barely eased the stress on our servers.

### Profiling the slow endpoints

Winnona and Chris used regular expressions to make short work of the server
logs, making a sorted list of the endpoints that took the most time to produce
results. As it turned out, `/users/swipable` and `/users/me` both took
incredible amounts of time to do anything.

(`/users/swipable` returns a list of all of the users that can be swiped on by
the current user. This excludes people who have been banned, the user has
blocked, the users who have blocked the current user, people that have already
been matched with, etc. `/users/me` returns information about the current
user.)

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

As we found out, requests that time out do not even make it into the Heroku
logs --- but they are still included in our logs, because the process isn't
killed. Just the proxy between the user and our process. So this additional
level of logging turned up routes that *didn't even show up* in the Heroku logs
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
be extensible to N people, where N was, say, three. So we created a so-called
"many to many" relationship using the `users_matches` auxiliary table. This
initially complicated matters, but in the end was reasonably easily optimized.

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
        # Can't swipe on self
        if user.id == g.u.id:
            continue

        # User must have accepted code of conduct
        if not user.accepted_coc:
            continue

        more_sanity_checks(user)

        swipe = most_recent_swipe_between(me, user)  # makes DB query

        # User shouldn't appear if you right swiped on them, or if you
        # unmatched the most recent match you had, etc
        some_sanity_checks(swipe)  # makes a couple DB queries

    # ...
```

Astute readers will notice that this makes *quite a few* database queries. In
fact, the number of database queries scales linearly with the number of users
on JumboSmash. This is, uh, bad. Consider:

* 300 (at the time) concurrent users
* a simple DB query takes on the order of 30ms

You do the math. We did and it wasn't pretty:

1 API call = (300 users in the DB) * (5 DB queries) * (30 ms per DB query) = 45 seconds

And, you know, Heroku times out after 30 seconds.

Perhaps the worst part of this, though, is that this entire route could be
transformed into a handful of medium-complicated SQL queries. We only didn't do
that up front because we were unfamiliar with the ORM library, SQLAlchemy, and
unwilling to have one endpoint be hand-written SQL. And, of course, we forgot
that this was the case before releasing. Since we didn't have realistically big
test data or any automated tests for the API, we completely failed to notice
that this route performed terribly. If latent performance issues constitute
technical debt, this was technical poverty.

So, as happens in all my stories, we called in the big guns: [Tom Hebb][tom]
and [Logan Garbarini][logan]. Bless them. Tom and I got to work transforming
the fustercluck of an endpoint into one large SQLAlchemy expression that
resulted in a reasonably fast SQL query. After some time I left him to it to
analyze the rest of the codebase.

[tom]: https://tchebb.me/
[logan]: https://logangarbarini.com/

As we found out later on in the process, the resulting SQLAlchemy concoction
was *heinous*, coming in at 55 non-blank lines. Thankfully, it only took one
other all-nighter with Logan much later on to tame it to something "reasonable"
like 48 non-blank lines (including some comments).

#### `/users` makes at least one query per user in the database

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
on their profile. That's pretty atrocious, and also easily avoidable with SQL.
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
We'll talk about `serialize` in a second.

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
user objects, the world probably shouldn't know about a user's `session_key`
(used to log in). That's sensitive information. In fact, the world probably
shouldn't know about most of the fields on the user object, so we only ever
want to serialize the so called "safe fields" of a database object (in
general). So we decided to add a field called `__safe_fields__` to every class
that represents a table in the database. For `User`, this looks like this:

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

[python-properties]: http://web.archive.org/web/20180717030835/http://stackabuse.com/python-properties/

A call to `serialize(some_user)` makes the following queries:

1. Fetch the `images` from the `user_images` table
1. Fetch the `react`s to this user's profile via the `users_reacts` table
1. Fetch the `tag`s on this user's profile via the `users_tags` table
1. Fetch the `event`s this user is attending via the `event_responses` table

This is all pretty inexcusable because it's still possible to smush this all
into one large SQL query instead. But here we are.

Now consider what happens when a `Match` is serialized, for example in the
event that `/users/me` is requested and the endpoint has to display all of the
user's `active_matches` (matches haven't been unmatched, members have not been
blocked, etc... the matches that should be displayed to the user):

1. Serialize the user, including all the fetches above
1. Additionally, fetch all of the user's `match`es via the `users_matches`
   table and serialize them
   * For each serialized `match`, since `users` is considered a "safe field" of
     a `match`, every user present in the match is re-fetched and
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
minute TTL. This introduced some new problems, like caching profile images and
bios that used to be more dynamic, but it worked.

#### Stop requesting `/users` so much and return relevant information elsewhere

While having the old version of the mobile app in the wild meant that we
couldn't change `/users` too much, we could encourage people to use the new
version. And the new version could definitely update its offline store of user
information from other places, thereby reducing overall load on `/users`.

We squeaked in more information everywhere we could without breaking API
compatibility: in the notification payloads, in `/users/swipable`, in new
match notifications, etc. This strategy really helped take some load off of the
extraordinarily expensive routes.

#### Database indexes, holy shit

I don't know what happened to us, but up until the end we lived in a world
where database indexes didn't exist. It wasn't until the `swipes` table was
growing approaching millions of rows that we realized that an index on the
`created_at` column would be helpful. Or that an index on `users.email` would
speed up literally every endpoint that required authentication. This turns a
linear lookup and string comparison into at **most** a *O(log n)* B-Tree
search. You could even make it a *O(1)* lookup using a hash table index.

Simple database indexes are so easy to set up and save so much time. Use them!

### Logging on the front-end

Readers of this post at this point will probably be asking, "what do you mean,
the new version of the app?" Well. As it turns out, the problems weren't *all*
to do with the server.

The mobile app was made with React Native, and we used the popular package
Redux to manage the "global state" of the app. This global state included
information about the current user (their name, bio, pictures, etc), as well as
similar information for every other user that was using our app.

We used another nifty package called `redux-logger`, which was extraordinarily
helpful during development. Every time our global Redux state changed,
`redux-logger` would log three things to the console: the old state, the action
that caused the state to change, and the new state that resulted from that
action. At least one part of the state was changed when a network request was
dispatched or returned; each network request caused two state changes, and by
extension, for the global state to be logged to the console four times.

And when you're swiping on people three times per second, you're logging the
global state to the console *12 times per second*.

Though we didn't know this at at the time, this logging was happening even when
there was no console to receive the logs (e.g. on an actual phone). In our
development using mobile phone simulators, this caused no issues at all. When
testing on real devices, this caused no issues at all. Even once we had shipped
to the App Store and used the app (before everyone else had signed up), there
were no issues. Why not?

Because the global state was small. We only had a couple of users (just the
JumboSmash team) using the app. So even though we were logging every user's
information a dozen times a second, there still wasn't that much to log, so we
didn't notice any problems.

Everything changed once the app was released, three hundred people signed up,
and the fire nation attacked. Every phone was constantly trying to log every
other user's information to an imaginary console, and with many users, the
phones just couldn't handle it. To say the app was sluggish would be an
understatement; it was unusable.

One of the major client-side speedups came from only logging if the app was in
development mode. Aside from that, the new version tried its best to help
reduce server load using the method described in the section above.

### Test with life-like conditions

We would have discovered almost every one of our problems ahead of time had one
of us set some time aside to set up a thousand fake users and see how the app
worked. We thought about doing it, but decided that ultimately it would take
too much time. Big oops. So, life lesson: test with realistic conditions. Make
sure your service can handle the load required of it.

### Learning to say 'fuck it'

Boy, were most people super jazzed about the app. Much of the feedback we got
was to the tune of

> "I hope that things are going better today! i know that you and your team
> have put in so much time and energy into the jumbo smash app and i'm sure you
> have the capacity to turn it around. and if not, that's very okay! you all
> did a lot of very hard work to get to this point!!!!"

or

> "the app is insanely good considering yall did it in a few months for free
> during school ... good shit"

or

> "I really appreciate the block feature btw"

or even just

> "thanks for making the app. Great work 👍👍"

We got high fives that week from people we didn't know existed.

And yet, the complaints about the initial downtime and occasional error rolled
in. Sarcastic, acidic, biting complaints. We got them in person, by email, by
Facebook Messenger, as app reviews, and even by text. Somehow our cell phone
numbers got around. This was extraordinarily disheartening for everybody on the
team. We had to set up a rotating schedule to respond to emails because it was
too soul-crushing for anybody to do for an extended period of time.

Look, learning technical things about problem solving and API design is great,
but it's not the most important thing. We developed a free app over the course
of the year, occasionally each pouring upwards of 20 hours per week into it. We
did this with no compensation or expectation of a reward. We did this *for our
classmates*. And maybe for the occasional high five.

At some point we all got together and decided to just stop responding. Who
cares if someone has duplicate pictures in their profile only sometimes because
some kind of obscure network race condition? Not us. It was senior week, and,
being seniors, we deserved to enjoy it at least a little. So we sat back, had a
beer or three, and enjoyed one another's company.
