"""Minimal repro of mistune footnote bug: fenced code blocks with indented
lines are truncated inside footnotes.

The REF_FOOTNOTE regex uses ``{1,4}(?! )`` for continuation lines, which
rejects lines indented more than 4 extra spaces (e.g. code inside a fenced
block within a footnote).

Run:  python3 mistune_footnote_bug_repro.py

Expected: footnote contains complete code block (3 lines) + trailing paragraph
Actual:   footnote contains only 1st line of code block; rest leaks to top level
"""
import mistune
from mistune.plugins.footnotes import footnotes

md = mistune.create_markdown(plugins=[footnotes])

text = """\
Text[^1].

[^1]: Before code:

    ```c
    int foo() {
      return 0;
    }
    ```

    After code.
"""

result = md(text)
print(result)

footnote_html = result.split('class="footnotes"')[1]
ok = True
if "return 0" not in footnote_html:
    print("BUG: code block truncated — 'return 0' missing from footnote")
    ok = False
if "After code" not in footnote_html:
    print("BUG: trailing paragraph missing from footnote")
    ok = False
if ok:
    print("OK: footnote rendered correctly")
else:
    print("\nThe continuation-line pattern {1,4}(?! ) in REF_FOOTNOTE rejects")
    print("lines indented >4 spaces, breaking fenced code blocks in footnotes.")
    print("Fix: change {1,4}(?! ) to {1,} in REF_FOOTNOTE regex.")
