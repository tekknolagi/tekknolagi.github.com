---
layout: page
title: Tree numbering
permalink: /tree-numbering/
---

<figure>
 <img class="invert" src="/assets/img/dominator-dfs-intervals.png" alt="Diagram from
 *Validating Dominator Trees for a Fast, Verified Dominance Test* by Blazy,
 Demange, and Pichardie showing a CFG, its domtree, and reverse post-order
 interval numbering of its domtree. " />
 <figcaption>Figure and below caption from <a href="/assets/img/dominators-verified.pdf">Validating Dominator Trees for a Fast, Verified Dominance Test</a> (PDF) by Blazy,
 Demange, and Pichardie.<br><br>
 Left: example CFG, with entry point 17, and nodes ordered in
reverse post-order (on the left). Center: its dominator tree (where, if <i>i</i> has <i>j</i> as a child, then <i>i</i> is the immediate dominator of <i>j</i>). Right: dominator tree (solid arrows), annotated with a DFS traversal (dotted arrows), and its
corresponding DFS intervals.</figcaption>
</figure>
