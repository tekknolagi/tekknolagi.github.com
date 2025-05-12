import pickle
import numpy as np

with open("word2vec.pkl", "rb") as f:
    data = pickle.load(f)

result = {}
for word, vec in data.items():
    result[word] = vec.tolist()

with open("word2vec_normal.pkl", "wb") as f:
    pickle.dump(result, f)
