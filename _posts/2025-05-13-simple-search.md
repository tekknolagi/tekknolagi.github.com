---
title: "A simple search engine from scratch*"
layout: post
---

\*if you include word2vec.

[Chris](https://www.chrisgregory.me/) and I spent a couple hours the other day
creating a search engine for my blog from "scratch". Mostly he walked me
through it because I only vaguely knew what word2vec was before this experiment.

The search engine we made is built on *word embeddings*. This refers to some
function that takes a word and maps it onto N-dimensional space (in this case,
N=300) where each dimension vaguely corresponds to some axis of meaning.

The idea behind the search engine is to embed each of my posts into this domain
by adding up the embeddings for the words in the post. For a given
search, we'll embed the search the same way. Then we can rank all posts by
their [cosine similarities](https://en.wikipedia.org/wiki/Cosine_similarity)
to the query.

The equation below might look scary but it's saying that the cosine similarity,
which is the cosine of the angle between the two vectors `cos(theta)`, is
defined as the dot product divided by the product of the magnitudes of each
vector. We'll walk through it all in detail.

<figure>
<img src="/assets/img/cosine-similarity.svg" />
<figcaption>Equation from Wikimedia's <a href="https://en.wikipedia.org/wiki/Cosine_similarity">Cosine similarity</a>
page.</figcaption>
</figure>

This is just one metric for query/page similarity. Other metrics include
[tf-idf](https://en.wikipedia.org/wiki/Tf%E2%80%93idf) or just plain substring
search. We chose this one because TODO.

## Embedding

We take for granted this database of the top 10,000 most popular word
embeddings, which is a 12MB pickle file that vaguely looks like this:

```
couch  [0.23, 0.05, ..., 0.10]
banana [0.01, 0.80, ..., 0.20]
...
```


Chris sent it to me over the internet. If you unpickle it, it's actually a
NumPy data structure: a dictionary mapping strings to `numpy.float32` arrays. I
wrote a script to transform this pickle file into plain old Python floats and
lists because I wanted to do this all by hand.

The loading code is straighforward: use the `pickle` library. The usual
security caveats apply, but I trust Chris.

```python
import pickle

def load_data(path):
    with open(path, "rb") as f:
        return pickle.load(f)

word2vec = load_data("word2vec.pkl")
```

You can print out `word2vec` if you like, but it's going to be a lot of output.
I learned that the hard way. Maybe print `word2vec["cat"]` instead. That will
print out the embedding.

To embed a word, we need only look it up in the enormous dictionary. A nonsense
or uncommon word might not be in there, though, so we return `None` in that
case instead of raising an error.

```python
def embed_word(word2vec, word):
    return word2vec.get(word)
```

To embed multiple words, we embed each one individually and then add up the
embeddings pairwise. If a given word is not embeddable, ignore it. It's only a
problem if we can't understand any of the words.

```python
def vec_add(a, b):
    return [x + y for x, y in zip(a, b)]

def embed_words(word2vec, words):
    result = [0.0] * len(next(iter(word2vec.values())))
    num_known = 0
    for word in words:
        embedding = word2vec.get(word)
        if embedding is not None:
            result = vec_add(result, embedding)
            num_known += 1
    if not num_known:
        raise SyntaxError(f"I can't understand any of {words}")
    return result
```

That's the basics of embedding: it's a dictionary lookup and vector adds.

```python
embed_words([a, b]) == vec_add(embed_word(a), embed_word(b))
```

Now let's make our "search engine index", or the embeddings for all of my
posts.

## Embedding all of the posts

Embedding all of the posts is a recursive directory traversal where we build up
a dictionary mapping path name to embedding.

```python
import os

def load_post(pathname):
    with open(pathname, "r") as f:
        contents = f.read()
    return normalize_text(contents).split()

def load_posts():
    # Walk _posts looking for *.md files
    posts = {}
    for root, dirs, files in os.walk("_posts"):
        for file in files:
            if file.endswith(".md"):
                pathname = os.path.join(root, file)
                posts[pathname] = load_post(pathname)
    return posts

post_embeddings = {pathname: embed_words(word2vec, words)
                   for pathname, words in posts.items()}

with open("post_embeddings.pkl", "wb") as f:
    pickle.dump(post_embeddings, f)
```

We do this other thing, though: `normalize_text`. This is because blog posts
are messy and contain punctuation, capital letters, and all other kinds of
nonsense. In order to get the best match, we want to put things like "CoMpIlEr"
and "compiler" in the same bucket.

```python
import re

def normalize_text(text):
    return re.sub(r"[^a-zA-Z]", r" ", text).lower()
```

We'll do the same thing for each query, too. Speaking of, we should test this
out. Let's make a little search REPL.

## A little search REPL

We'll start off by using some Python's built-in REPL creator library, `code`.
We can make a subclass that defines a `runsource` method. All it really needs
to do is process the `source` input and return a falsy value (otherwise it
waits for more input).

```python
import code

class SearchRepl(code.InteractiveConsole):
    def __init__(self, word2vec, post_embeddings):
        super().__init__()
        self.word2vec = word2vec
        self.post_embeddings = post_embeddings

    def runsource(self, source, filename="<input>", symbol="single"):
        for result in self.search(source):
            print(result)
```

Then we can define a `search` function that pulls together our existing
functions. Just like that, we have a search:

```python
class SearchRepl(code.InteractiveConsole):
    # ...
    def search(self, query_text, n=5):
        # Embed query
        words = normalize_text(query_text).split()
        try:
            query_embedding = embed_words(self.word2vec, words)
        except SyntaxError as e:
            print(e)
            return
        # Cosine similarity
        post_ranks = {pathname: vec_cosine_similarity(query_embedding,
                                                      embedding) for pathname,
                      embedding in self.post_embeddings.items()}
        posts_by_rank = sorted(post_ranks.items(),
                               reverse=True,
                               key=lambda entry: entry[1])
        top_n_posts_by_rank = posts_by_rank[:n]
        return [path for path, _ in top_n_posts_by_rank]
```

Yes, we have to do a cosine similarity. Thankfully, the Wikipedia math snippet
translates almost 1:1 to Python code:

```python
import math

def vec_norm(v):
    return math.sqrt(sum([x*x for x in v]))

def vec_cosine_similarity(a, b):
    assert len(a) == len(b)
    a_norm = vec_norm(a)
    b_norm = vec_norm(b)
    dot_product = sum([ax*bx for ax, bx in zip(a, b)])
    return dot_product/(a_norm*b_norm)
```

Finally, we can create and run the REPL.

```python
sys.ps1 = "QUERY. "
sys.ps2 = "...... "

repl = SearchRepl(word2vec, post_embeddings)
repl.interact(banner="", exitmsg="")
```

This is what interacting with it looks like:

```console?prompt=QUERY.
QUERY. type inference
_posts/2024-10-15-type-inference.md
_posts/2025-03-10-lattice-bitset.md
_posts/2025-02-24-sctp.md
_posts/2022-11-07-inline-caches-in-skybison.md
_posts/2021-01-14-inline-caching.md
QUERY.
```

This is a sample query from a very small dataset (my blog). It's a pretty good
search result, but it's probably not representative of the overall search
quality. Chris says that I should cherry-pick "because everyone in AI does".

Okay, that's really neat. But most people who want to look for something on
my website do not run for their terminals. Though my site is expressly designed
to work well in terminal browsers such as Lynx, most people are already in a
graphical web browser. So let's make a search front-end.

## A little web search
