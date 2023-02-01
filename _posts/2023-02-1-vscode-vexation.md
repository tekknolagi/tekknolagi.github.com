---
title: VSCode vexation
layout: post
date: 2023-02-01
---

## Prologue

Tom, of the Tom Chronicles, came to me in the night.

"Max," he said, "maybe you can help me with something."

"Tom," I said, "I never thought this day would come."

"So I have this VSCode question..."

## So I have this bug, but that's not important right now

"I'm down a rabbit hole as usual"

* Went to figure out why it was crashing
* Saw a [GitHub issue][sudoissue] from September 8, 2022
* Issue closed out September 16, 2022 but released September 29, 2022
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
      * To be clear, as of time of post writing, VSCode is
        [using version 19][electrondep]
    * If you open up the tag that VSCode claims to use today, v19.1.9, you can
      see the fix is [not included][fixnotincluded]
      * If it *had been* included, the green/additions would be after the
        trailing brace and dedented, as in the [actual fix][actualfix]
    * Furthermore, the entire `UtilityProcess` API, which VSCode
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

[^insiders]: [VSCode Insiders][insiders] is like a beta build of VSCode.

[insiders]: https://code.visualstudio.com/insiders/

First of all, we acknowledge that VSCode does not claim to be built off of the
OSS codebase. However, we find it surprising that the official builds:

* Don't use the same dependencies that the OSS build does
* We see only half of the picture! This is inconsistent. Microsoft has
  committed client code that uses the `UtilityProcess` API without declaring
  any dependencies (vendored or not) that actually implement that API!

So we have VSCode using an API that does not exist in the library it claims to
be using. This leads us to believe that the Insiders build of VSCode uses a
patched version of Electron, the source to which we cannot find anywhere.

What else is in that fork?
