---
name: push
description: Publish committed work of OfxRawHost to GitHub. Use when the user asks to push, publish, cut a dev release, publish a pre-release, promote a release branch to stable, or run /skill:push. Confirms the target first, selects the account that owns the repository, and knows that a push to release/vX.X.X publishes a dev pre-release tag and a vX.X.X tag publishes a full release. Commits nothing, so run /skill:commit first.
---

# Push

Publish local commits to GitHub. Read `AGENTS.md` at the repository root first.
This skill publishes only. It never writes a commit, and it never edits the code.

## Hard rules

- Never push unless the user asks for a push in this request.
- Never create a stable release unless the user asks for a stable release in this request.
- Never push a `vX.X.X` tag unless the user asks for that tag in this request.
- Ask for the release kind before a release action, and for the version before you create a release branch.
- Confirm the target with the user before the push.
- Run `gh auth status` and select the account that owns the repository before the push.
- Never wait for the workflow. Report the push and stop.

## What a push publishes

The workflow is `.github/workflows/release.yml`. The `prepare` job reads the
version, the tag, and the pre-release flag from the git ref.

- A push to `release/vX.X.X` publishes the tag `vX.X.X-dev-<short sha>` as a GitHub pre-release. The short sha gives every push its own tag, so a later pre-release does not overwrite the last one. The build keeps the numeric version `X.X.X`.
- A push of the tag `vX.X.X` publishes a full release. The tag carries the version.
- A push to any other branch publishes nothing.
- The publish job copies `docs/releases/<tag>.md` into the release body, so that file has to exist before the tag is pushed.

## Step 1: read the state

```bash
git branch --show-current
git status --short
git fetch origin
git status -sb
git log --oneline -5
```

Stop when the tree holds uncommitted work. Ask the user to run `/skill:commit`
first, or ask whether to push the commits that are already there.

Read the remote owner, which Step 4 needs.

```bash
git remote get-url origin
```

## Step 2: decide what this push publishes

Read the current branch, then take one of the three paths below.

### Path A: the branch is `release/vX.X.X`

This is a dev release of version `X.X.X`. Keep the branch version and do not bump
it. Skip Path B and go to Step 3.

### Path B: any other branch

Ask the user one question: dev release or stable release.

For a dev release, recommend a version. Read the tags and the work that is not
published yet.

```bash
git fetch --tags
git tag --sort=-v:refname | rg '^v[0-9]+\.[0-9]+\.[0-9]+$' | head -1
git log --oneline <latest-tag>..HEAD
git branch -a | rg 'release/v'
```

Recommend one version with this rule.

- New features or user-visible changes: bump the minor version.
- Bug fixes only: bump the patch version.
- Breaking changes: bump the major version.

Report the latest stable tag, the reason for the recommendation, and the version.
Ask the user to confirm the version or to give another one.

The dev release comes from a `release/vX.X.X` branch.

1. Run `git checkout main` and `git pull --ff-only`.
2. Run `git checkout -b release/vX.X.X` for a new version, or `git checkout release/vX.X.X` for an existing branch.
3. Stop when the branch holds uncommitted work. Ask the user to run `/skill:commit`.

For a stable release, name the version in the confirmation question and continue to
Path C after the push of `main`.

### Path C: a stable release (only when the user asks for it)

1. Run `git checkout main` and `git pull --ff-only`.
2. Run `git merge --no-ff release/vX.X.X`.
3. Write or update `docs/releases/vX.X.X.md` before the tag. When the file is missing, ask the user to write it first, because it becomes the release body.
4. Continue to Step 3 with the target `main` and then the tag `vX.X.X`.

## Step 3: confirm the target

Ask the user one question, and name the branch, the tag, and the kind of release.
Show the exact push command in the options. Wait for the answer before Step 4.

## Step 4: select the account that owns the repository

```bash
gh auth status
```

Compare the active account with the owner in the remote URL. When they differ,
select the owner.

```bash
gh auth switch --user <owner>
```

Then confirm that the owner is the active account.

```bash
gh auth status
```

## Step 5: push

Run only the path that matches the confirmed target. The `<short sha>` of the new
tag is the first six characters of `HEAD`.

Path A, and Path B after the branch is ready:

```bash
git push -u origin release/vX.X.X
```

Path C:

```bash
git push origin main
git tag vX.X.X
git push origin vX.X.X
```

## Step 6: report and stop

Report the branch or the tag that was pushed, and the tag the workflow will
publish. Do not sleep, poll, or watch the GitHub Actions run. Stop there.
