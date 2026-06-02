---
title: "Lively code"
layout: post
---

TODO(on: date(...), to: '#slack-channel')
https://github.com/sindresorhus/eslint-plugin-unicorn/blob/main/docs/rules/expiring-todo-comments.md
https://github.com/jbreckmckye/todo-or-else
todocheck (preslavmihaylov) and tickgit, leasot, imdone — parse TODOs referencing issues and fail CI when the referenced issue is closed. the TODO dies when its issue resolves.

TODO(username)

TODO(issue number)

* even with non-github, github can auto-link certain text patterns like D12345
* github issue auto-closing with "Fixes #123"

LINT.IfChange https://github.com/simonepri/ifttt-lint
https://filiph.net/text/ifchange-thenchange.html
https://www.chromium.org/chromium-os/developer-library/guides/development/keep-files-in-sync/

doctests

Danger / DangerJS -- PR-level rules ("you touched schema.rb but not a migration", "changed API but not CHANGELOG") encoded in JS. IfChange at review granularity.

Cog (Ned Batchelder), embedme, markdown-code-runner, mdformat -- embed/generate content in-place and re-check it's current in CI. bindgen (which you use) is the same pattern across the C<->Rust boundary.

Codegen markers: Go's // Code generated ... DO NOT EDIT + go generate, protobuf/IDL regeneration, gazelle for BUILD files.


- Piranha (Uber) — once a feature flag is permanently enabled, it deletes the now-dead branches automatically. The end state of an expiring annotation: the code removes itself.

<!---
  Linking that breaks the build when the target moves:
  - Rustdoc intra-doc links [`Foo`] with #![deny(rustdoc::broken_intra_doc_links)].
  - Javadoc {@link} under doclint; Sphinx :ref: / intersphinx with nitpicky mode erroring on dangling refs; Doxygen \ref.
-->

  Liveness/quality scanners

  - Link liveness: lychee, markdown-link-check, linkinator, remark-validate-links.
  - Spelling with inline allow-lists: typos (Rust), codespell, cspell.
