import argparse
import code
import json
import math
import os
import pickle
import random
import re
import sys
from typing import Optional

from matplotlib import pyplot as plt

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
            return []
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

    random.seed(42)

    max_n_keywords = 4
    max_top_k = 11
    n_query_samples = 30

    accuracies = compute_top_k_accuracy(
        db,
        eval_set,
        max_n_keywords,
        max_top_k,
        n_query_samples,
    )
    with plt.xkcd():
        plot_top_k_accuracy(accuracies, max_n_keywords, max_top_k)


def plot_top_k_accuracy(
    accuracies: list[list[float]],
    max_n_keywords: int,
    max_top_k: int,
) -> None:
    plt.figure(figsize=(10, 6))
    plt.title("TOP K ACCURACY")
    plt.xlabel("TOP K")
    plt.ylabel("ACCURACY")

    for n_keywords in range(1, max_n_keywords + 1):
        xs = []
        ys = []
        for top_k in range(1, max_top_k + 1):
            accuracy = accuracies[n_keywords - 1][top_k - 1]
            xs.append(top_k)
            ys.append(accuracy)

        label = f"{n_keywords} KEYWORDS" if n_keywords > 1 else "1 KEYWORD"
        plt.plot(xs, ys, label=label, marker="o")

    plt.xticks(range(1, max_top_k + 1))
    percentage_formatter = plt.FuncFormatter(lambda x, _: f"{x:.0%}")
    plt.gca().yaxis.set_major_formatter(percentage_formatter)
    plt.gca().set_ylim(0, 1)

    plt.legend()
    plt.grid()
    plt.show()


def safe_index[T](xs: list[T], x: T) -> Optional[int]:
    try:
        return xs.index(x)
    except ValueError:
        return None


def compute_top_k_accuracy(
    db: DB,
    eval_set: dict[str, str],
    max_n_keywords: int,
    max_top_k: int,
    n_query_samples: int,
) -> list[list[float]]:
    counts = [[0] * max_top_k for _ in range(max_n_keywords)]
    for n_keywords in range(1, max_n_keywords + 1):
        for post_id, keywords_str in eval_set.items():
            for _ in range(n_query_samples):
                # Construct a search query by sampling keywords
                keywords = keywords_str.split(" ")
                sampled_keywords = random.choices(keywords, k=n_keywords)
                query = " ".join(sampled_keywords)

                # Determine the rank of the target post in the search results
                ids = db.search(query, n=max_top_k)
                rank = safe_index(ids, post_id)

                # Increment the count of the rank
                if rank is not None and rank < max_top_k:
                    counts[n_keywords - 1][rank] += 1

    accuracies = [[0.0] * max_top_k for _ in range(max_n_keywords)]
    for i in range(max_n_keywords):
        for j in range(max_top_k):
            # Divide by the number of samples to get the average across samples and
            # divide by the size of the eval set to get accuracy over all posts.
            accuracies[i][j] = counts[i][j] / n_query_samples / len(eval_set)

            # Accumulate accuracies because if a post is retrieved at rank i,
            # it was also successfully retrieved at all ranks j > i.
            if j > 0:
                accuracies[i][j] += accuracies[i][j - 1]

    return accuracies


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
