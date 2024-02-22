---
title: Git
layout: page
---

There are some aliases and Git addons that I use to make my life easier.

## Aliases

I have a shell alias that maps `git` to `g`, but that's about as far as I go. I
don't have `gco` or things like that because I feel like that would get
confusing for me.

I have an alias called `git recent` that pretty-prints the branches I have
poked at most recently:

```
[alias]
	recent = ! git branch --sort=-committerdate --format=\"%(committerdate:relative)%09%(refname:short)\" | head -10
```

which looks like:

```
$ g recent
45 minutes ago	at-listiter
59 minutes ago	mb-mul-add
61 minutes ago	mb-lshift
2 hours ago	cinder/3.8
$
```

I have an alias called `git smartlog` (and `git sl`) that emulates [`hg
smartlog`](https://www.mercurial-scm.org/wiki/SmartlogExtension), which I have
gotten used to using at work.

```
[alias]
	 smartlog = log --graph --pretty=format:'commit: %C(bold red)%h%Creset %C(red)[%H]%Creset %C(bold magenta)%d %Creset%ndate: %C(bold yellow)%cd %Creset%C(yellow)%cr%Creset%nauthor: %C(bold blue)%an%Creset %C(blue)[%ae]%Creset%n%C(cyan)%s%n%Creset'
```

## Patterns

I like to write descriptive commit summaries, especially explaining motivation
or interesting debugging stories. They are helpful to come back to later when I
inevitably `git blame` myself.

I almost always stage changes with `add -p`. This way I can split out
changes more finely than per-file. This helps me structure commits into neat
little series instead of one giant blob.

I almost always commit with `commit --verbose`. This way I can page through my
changes when describing them.

## Configuration options

In my global `.gitconfig` I have:

```
[commit]
	template = /home/username/.gitmessage
```

This helps keep my commit messages consistent.

## Addons and CLI programs

I have [`git absorb`](https://github.com/tummychow/git-absorb), which is an
easier way to do `git commit --fixup` and `git rebase -i --autosquash`.

I have [`tig`](https://github.com/jonas/tig) which I use to page through
commits.

I use [`git filter-repo`](https://github.com/newren/git-filter-repo) to rebase
files and directories out of existence.

I use [`git branchless`](https://github.com/arxanas/git-branchless), which does
in fact support branches. I use it primarily for `git next` and `git prev`. I
have occasionally also used `git restack`, though these days I tend to use
`rebase -i`, edit the commit (`edit`), and continue the rebase.

In the past I used [gitolite](https://github.com/sitaramc/gitolite) extensively
to host my own Git server with multiple users and whatnot.

## Vim plugins

I have [`vim-fugitive`](https://github.com/tpope/vim-fugitive) installed and
use it regularly.

## In-repo issue trackers

* [git-bug](https://github.com/MichaelMure/git-bug)
* [git-appraise](https://github.com/google/git-appraise)
* [git-issue](https://github.com/dspinellis/git-issue)

## Other

I think [Jujutsu/`jj`](https://github.com/martinvonz/jj) is very neat. Also
[Game of Trees](https://gameoftrees.org/). Also
[dura](https://github.com/tkellogg/dura). Also
[gitless](https://github.com/gitless-vcs/gitless). Also
[Sapling](https://sapling-scm.com/).

Learn how to [see the history of a
method](https://calebhearth.com/git-method-history).
