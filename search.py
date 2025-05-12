import argparse
import code
import math
import os
import pickle
import readline
import sys

def load_data(path):
    with open(path, "rb") as f:
        return pickle.load(f)

def vec_add(a, b):
    return [x + y for x, y in zip(a, b)]

def vec_norm(v):
    return math.sqrt(sum([x*x for x in v]))

def vec_cosine_similarity(a, b):
    assert len(a) == len(b)
    a_norm = vec_norm(a)
    b_norm = vec_norm(b)
    dot_product = sum([ax*bx for ax, bx in zip(a, b)])
    return dot_product/(a_norm*b_norm)

def embed_words(word2vec, words):
    result = [0.0] * len(next(iter(word2vec.values())))
    num_known = 0
    for word in words:
        embedding = word2vec.get(word)
        if embedding is not None:
            result = vec_add(result, embedding)
            num_known += 1
    if not num_known:
        raise SyntaxError(f"What are you smoking? I can't understand any of {words}")
    return result

class SearchRepl(code.InteractiveConsole):
    def __init__(self, word2vec, post_embeddings):
        super().__init__()
        self.word2vec = word2vec
        self.post_embeddings = post_embeddings

    def runsource(self, source, filename="<input>", symbol="single"):
        # Embed query
        words = source.split()
        try:
            query_embedding = embed_words(self.word2vec, words)
        except SyntaxError as e:
            print(e)
            return
        # Cosine similarity with
        post_ranks = {pathname: vec_cosine_similarity(query_embedding,
                                                      embedding) for pathname,
                      embedding in self.post_embeddings.items()}
        results = sorted(post_ranks.items(), reverse=True, key=lambda entry: entry[1])[:5]
        for path, _ in results:
            print(path)

sys.ps1 = "QUERY. "
sys.ps2 = "...... "

def repl_main(args):
    word2vec = load_data("word2vec_normal.pkl")
    post_embeddings = load_data("post_embeddings.pkl")
    repl = SearchRepl(word2vec, post_embeddings)
    repl.interact(banner="", exitmsg="")

def load_post(pathname):
    with open(pathname, "r") as f:
        contents = f.read()
    return contents.split()

def load_posts():
    # Walk _posts looking for *.md files
    posts = {}
    for root, dirs, files in os.walk("_posts"):
        for file in files:
            if file.endswith(".md"):
                pathname = os.path.join(root, file)
                posts[pathname] = load_post(pathname)
    return posts

def process_site_main(args):
    word2vec = load_data("word2vec_normal.pkl")
    posts = load_posts()
    post_embeddings = {pathname: embed_words(word2vec, words) for pathname, words in posts.items()}
    with open("post_embeddings.pkl", "wb") as f:
        pickle.dump(post_embeddings, f)

def main():
    parser = argparse.ArgumentParser()
    parser.set_defaults(func=repl_main)
    subparsers = parser.add_subparsers()

    repl = subparsers.add_parser("repl")
    repl.set_defaults(func=repl_main)

    process_site = subparsers.add_parser("process_site")
    process_site.set_defaults(func=process_site_main)

    args = parser.parse_args()
    args.func(args)

if __name__ == "__main__":
    main()
