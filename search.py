import argparse
import code
import json
import math
import os
import pickle
import random
import re
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
        raise SyntaxError(f"I can't understand any of {words}")
    return result

def normalize_text(text):
    return re.sub(r"[^a-zA-Z]", r" ", text).lower()

class DB:
    def __init__(self, word2vec, post_embeddings):
        self.word2vec = word2vec
        self.post_embeddings = post_embeddings

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
        return [path for path, _ in sorted(post_ranks.items(), reverse=True, key=lambda entry: entry[1])[:n]]

class SearchRepl(code.InteractiveConsole):
    def __init__(self, db):
        super().__init__()
        self.db = db

    def runsource(self, source, filename="<input>", symbol="single"):
        for result in self.db.search(source):
            print(result)

sys.ps1 = "QUERY. "
sys.ps2 = "...... "

def repl_main(args):
    word2vec = load_data("word2vec_normal.pkl")
    post_embeddings = load_data("post_embeddings.pkl")
    db = DB(word2vec, post_embeddings)
    repl = SearchRepl(db)
    repl.interact(banner="", exitmsg="")

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

def process_site_main(args):
    word2vec = load_data("word2vec_normal.pkl")
    posts = load_posts()
    post_embeddings = {pathname: embed_words(word2vec, words) for pathname, words in posts.items()}
    with open("post_embeddings.pkl", "wb") as f:
        pickle.dump(post_embeddings, f)
    with open("post_embeddings.json", "w") as f:
        json.dump(post_embeddings, f, indent=None, separators=(",", ":"))

def build_index_main(args):
    word2vec = load_data("word2vec_normal.pkl")
    index = {}
    with open("vecs.jsonl", "w") as f:
        for word, embedding in word2vec.items():
            start = f.tell()
            json.dump(embedding, f, indent=None, separators=(",", ":"))
            end = f.tell()
            index[word] = [start, end-start]
    with open("index.json", "w") as f:
        json.dump(index, f, indent=None, separators=(",", ":"))

eval_set = \
{
"_posts/2024-10-27-on-the-universal-relation.md": "database relation universal tuple function",
"_posts/2024-10-15-type-inference.md": "type infer inference union static",
"_posts/2024-08-25-precedence-printing.md": "operator precedence pretty print parenthesis",
"_posts/2023-11-04-ninja-is-enough.md": "build system compile graph dependency",
"_posts/2024-01-13-typed-c-extensions.md": "foreign function interface type extension",
"_posts/2020-10-07-compiling-a-lisp-8.md": "condition if branch jump label",
"_posts/2024-05-19-weval.md": "projection compile interpret code meta",
"_posts/2019-03-11-understanding-the-100-prisoners-problem.md": "probability strategy game visualization simulation",
"_posts/2018-05-29-how-to-mess-with-your-roommate.md": "prank graphics error frustration roommate",
"_posts/2022-03-26-discovering-basic-blocks.md": "basic block python graph jump",
"_posts/2024-10-22-row-poly.md": "row type system static infer",
"_posts/2024-07-10-scrapscript-tricks.md": "trick compile fast small encode",
}

def eval_main(args) -> None:
    word2vec = load_data("word2vec_normal.pkl")
    post_embeddings = load_data("post_embeddings.pkl")
    db = DB(word2vec, post_embeddings)

    top_k = 10
    n_keywords = 3

    random_seed = 42
    random.seed(random_seed)

    # Map from video ID to the number of keywords sampled to the index of the video ID in the results
    results: dict[str, int] = {}
    for path, keywords_str in eval_set.items():
        keywords = keywords_str.split(" ")

        # Construct a search query by sampling keywords
        sampled_keywords = keywords[:n_keywords]
        query = " ".join(sampled_keywords)

        # Determine the index of the target video in the search results
        ids = db.search(query, n=top_k)
        try:
            rank = ids.index(path)
        except ValueError:
            rank = -1

        results[path] = rank

    top_k_threshold = 3
    print("Results:")
    print("Path\tRank")
    n_lt_k = 0
    for path, rank in results.items():
        print(f"{path}\t{rank}")
        if 0 <= rank < top_k_threshold:
            n_lt_k += 1

    top_k_accuracy = n_lt_k / len(results)
    print(f"Top-{top_k_threshold} accuracy: {top_k_accuracy:.2%}")

def main():
    parser = argparse.ArgumentParser()
    parser.set_defaults(func=repl_main)
    subparsers = parser.add_subparsers()

    repl = subparsers.add_parser("repl")
    repl.set_defaults(func=repl_main)

    process_site = subparsers.add_parser("process_site")
    process_site.set_defaults(func=process_site_main)

    build_index = subparsers.add_parser("build_index")
    build_index.set_defaults(func=build_index_main)

    eval = subparsers.add_parser("eval")
    eval.set_defaults(func=eval_main)

    args = parser.parse_args()
    args.func(args)

if __name__ == "__main__":
    main()
