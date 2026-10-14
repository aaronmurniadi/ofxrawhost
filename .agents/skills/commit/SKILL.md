---
name: commit
description: Build, test, and commit the working tree of OfxRawHost as one reviewable commit, without pushing. Use when the user asks to commit, make a commit, save the work in git, stage the changes, or run /skill:commit. Publishes nothing, so run /skill:push afterwards to publish.
---

# Commit

Turn the working tree into one reviewable commit. Do not push. Run `/skill:push` to
publish the commit.

Read `AGENTS.md` at the repository root first.

## Hard rules

- Never run `git push`.
- Never add a co-author trailer that names an AI agent or a bot.
- Never commit a broken build or a failing self test.
- Keep one logical change in one commit.

## Step 1: read the state

```bash
git branch --show-current
git status --short
git diff --stat
git log --format=%B -n 3
```

The last command shows the message style of the branch. Match it. The subject
line is imperative and short, and the body states why the change is made and then
what it does.

## Step 2: build and test

```bash
cmake --build build -j 8
./build/OfxRawHost.app/Contents/MacOS/OfxRawHost --selftest
```

The self test prints one `ok` line per check and fails on the first broken check.
When the build or a check fails, stop and report the failure. Do not commit.

## Step 3: drop files that do not belong

A headless run of the binary can leave a file in the tree, for example `imgui.ini`
at the root. Remove such a file. Check that `git status --short` holds only the
intended change.

## Step 4: split the work when it is more than one change

When the tree holds changes that do not belong together, ask the user one
question: one commit for all of it, or one commit per change. A commit that mixes
a fix and a feature cannot be reverted on its own.

## Step 5: write the release note

On a `release/vX.X.X` branch, `docs/releases/vX.X.X-dev.md` becomes the body of the
next pre-release. Add one bullet for each user-visible change of this commit. Match
the style of the bullets that are already there. The commit carries the note.

## Step 6: commit

```bash
git add -A
git commit -F - <<'MSG'
Short imperative subject

Why the change is made, in one or two sentences. Then what it does, and which
files or behaviours it touches when that is not obvious from the subject.
MSG
```

Then read back the result.

```bash
git log --oneline -1
git rev-parse --short=6 HEAD
git status --short
```

## Step 7: report and stop

Report the short hash, the subject, and the files in the commit. State that the
commit is local. Tell the user that `/skill:push` publishes it. Stop there.
