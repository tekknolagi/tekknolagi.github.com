# Generate diagrams by running:
#   uv run --with cogapp python -m cogapp -p "$(cat prelude.py)" -r article.md
# or
#   python3 -m cogapp -p "$(cat prelude.py)" -r article.md
# if you already have cogapp installed.
import subprocess
import hashlib
def dot(dot_source, out_file_name=None, url=None):
    input_bytes = dot_source.encode('utf-8')
    if out_file_name is None:
        out_file_name = hashlib.md5(input_bytes).hexdigest() + ".svg"
    if url is None:
        url = out_file_name
    subprocess.check_output(['dot', '-Tsvg', '-o', out_file_name], input=input_bytes)
    cog.out(f'<object data="{url}" type="image/svg+xml"></object>')
