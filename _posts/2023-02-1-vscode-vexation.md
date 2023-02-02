---
title: VS Code vexation
layout: post
date: 2023-02-01
co_authors: Tom Hebb
---

## Prologue

Tom, of the Tom Chronicles, came to me in the night.

"Max," he said, "maybe you can help me with something."

"Tom," I said, "I never thought this day would come."

"So I have this VS Code question..."

## I have this bug, but that's not important right now

"I'm down a rabbit hole as usual."

Tom is often three levels deep in some investigation to scratch an itch. In the
past, this has manifested itself in fixes to audio drivers, the Linux kernel,
and more.

He only wanted to figure out why VS Code would not format a Python file. Turns
out, the VS Code Python extension was crashing. Through some debugging and
internet searching, he found a [GitHub issue][sudoissue] from September 8,
2022. It turns out that there was a bug in VS Code related to setuid binaries:
it wasn't possible to `exec` a setuid binary from an extension, because the
extension host process was launched with a Linux flag that prevented it from
gaining any privileges.

The issue was closed out on September 16, 2022 but the fix was released
September 29, 2022.

* Looked at the [PR that closed the issue][allegedsudofix] and there is no
  evidence that it fixes the issue
  * If you look at the files changed, the changes in the PR do not obviously
    fix the described issue
  * The changes look like a typical upstream Electron version bump (updating
    version, updating a hash, and fixing some API breakages)
  * Perhaps they were being sloppy with the PR naming and had folded in the fix
    without mentioning it in the PR description
  * But they didn't: if you look at the files changed, nothing relates to the
    issue at all
  * Conclusion: it was fixed upstream in Electron and the version bump
    indirectly incorporates that fix
  * BUT: if you go and [compare the versions of Electron][versioncompare]
    bumped in the PR, there is *also* no fix for the issue
  * M: "But Tom, there are a lot of changes there---how do you know you are not
    overlooking the changes by accident?"
  * T: "To that I say, 'I know what [the fix] is because I tracked it down...
    and it does not appear in the Electron version they bumped to'"
    * It got committed October 20, 2022, after the release
    * *And*, it was never committed to the Electron 19 branch and to this day
      it is not in the Electron 19 branch (it is in later versions)
      * To be clear, as of time of post writing, VS Code is
        [using version 19][electrondep]
    * If you open up the tag that VS Code claims to use today, v19.1.9, you can
      see the fix is [not included][fixnotincluded]
      * If it *had been* included, the green/additions would be after the
        trailing brace and dedented, as in the [actual fix][actualfix]
    * Furthermore, the entire `UtilityProcess` API, which VS Code
      Insiders[^insiders] has used [since at least June 2022][onbydefault], is
      not present in the Electron 19 branch. The whole API is not there, and
      was not committed to any upstream Electron branch until October 2022.
      Even then, it was [only added to Electron 22][featUtilityProcess] (as
      noted by the `trop` porting bot).

[sudoissue]: https://github.com/microsoft/vscode/issues/160380
[allegedsudofix]: https://github.com/microsoft/vscode/pull/161027
[versioncompare]: https://github.com/electron/electron/compare/v19.0.12...v19.0.17
[actualfix]: https://github.com/electron/electron/pull/34980/commits/c3dff10a48f5edb23b2b0340c1849dc04db180bc
[electrondep]: https://github.com/microsoft/vscode/blob/e3da120e0808f36e45e6783b611cc943d7fdd61c/package.json#L146
[fixnotincluded]: https://github.com/electron/electron/blob/v19.1.9/patches/chromium/allow_new_privileges_in_unsandboxed_child_processes.patch
[onbydefault]: https://github.com/microsoft/vscode/pull/152470
[featUtilityProcess]: https://github.com/electron/electron/pull/34980

[^insiders]: [VS Code Insiders][insiders] is like a beta build of VS Code.

[insiders]: https://code.visualstudio.com/insiders/

## Surely this is a mistake

Is it? And don't call me Shirley.

We acknowledge that VS Code does not claim to be built off of the OSS codebase.
However, we find it surprising that we see only half of the picture! This is
inconsistent. Microsoft has committed client code that uses the
`UtilityProcess` API without declaring any dependencies (vendored or not) that
actually implement that API!

So we have VS Code using an API that does not exist in the library it claims to
be using. This leads us to believe that the Insiders build of VS Code uses a
patched version of Electron, the source to which we cannot find anywhere.

This [wiki page][nativecrash] further corroborates the hypothesis:

[nativecrash]: https://github.com/microsoft/vscode/wiki/Native-Crash-Issues/749bcb12d315f430e86170f1bc8d5cafc5c67dbc

> Most likely, the crash comes from an Insiders or Stable version of Code.
> Those versions use an internal Electron, and symbol files for the internal
> Electron along with Insiders and Stable are available at
> microsoft/vscode-electron-prebuilt.

(Where `vscode-electron-prebuilt` is a private repository.)

## Conclusion

Although it's common for companies to maintain both a proprietary and an
open-source version of their software (e.g. Chrome vs Chromium), it's generally
expected that the proprietary version is built from the open-source code with
some "secret sauce" added---e.g. product features like Microsoft Live Share.
What we see here is not that. Microsoft is maintaining a forked version of
Electron---which isn't where any of VS Code's product features live---without
clearly disclosing that fact or allowing open-source developers to audit their
changes. They clearly add large, incomplete features to that fork before
upstream Electron developers or the general public have a chance to review
them.

In Microsoft's [VS Code FAQ][faq], they state that

[faq]: https://code.visualstudio.com/docs/supporting/faq

> Microsoft Visual Studio Code is a Microsoft licensed distribution of 'Code -
> OSS' that includes Microsoft proprietary assets (such as icons) and features
> (Visual Studio Marketplace integration, small aspects of enabling Remote
> Development). While these additions make up a very small percentage of the
> overall distribution code base, it is more accurate to say that Visual Studio
> Code is "built" on open source, rather than "is" open source, because of
> these differences.

We've demonstrated here that the differences go far beyond superficial product
features, instead extending into Electron itself. `UtilityProcess` is part of a
[huge overhaul][sandboxing] of VS Code's security model. Changes to a program's
security model are one of the most important things to do in the open, and we
find it concerning that Microsoft seems to be in the habit of shipping such
code before making it public.

[sandboxing]: https://code.visualstudio.com/blogs/2022/11/28/vscode-sandbox
