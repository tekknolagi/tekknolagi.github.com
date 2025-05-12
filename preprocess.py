import pickle
import os

def load_data():
    with open("word2vec_normal.pkl", "rb") as f:
        return pickle.load(f)

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
        raise SyntaxError(f"What are you smoking? I can't understand any of {words}")
    return result

def main():
    word2vec = load_data()
    posts = load_posts()
    post_embeddings = {pathname: embed_words(word2vec, words) for pathname, words in posts.items()}
    with open("post_embeddings.pkl", "wb") as f:
        pickle.dump(post_embeddings, f)

if __name__ == "__main__":
    main()
