---
name: release
description: Prepare a dev release or a stable release of OfxRawHost. Use when the user asks to release, cut a release, publish a dev release, publish an alpha, promote a release branch to stable, or run /skill:release. On a release/vX.X.X branch it assumes a dev release of the branch version. Otherwise it asks for the release kind and the version before any push.
---

# Release

Run a dev release or a stable release of OfxRawHost. Read `AGENTS.md` first.

## Hard rules

- Never publish a stable release unless the user asks for a stable release in this request.
- Never push a `vX.X.X` tag unless the user asks for that tag in this request.
- Check the current branch first. On `release/vX.X.X`, the request is a dev release of version `X.X.X`; do not ask for the kind and do not bump the version.
- Off a release branch, ask for the release kind first and the version second.
- Never run `git push` until the user confirms the push.
- Before any `git push`, run `gh auth status` and select the account that owns the repo.

## Step 1: read the current branch

Run `git branch --show-current`.

- If the branch is `release/vX.X.X`, this is a dev release of version `X.X.X`. Keep the branch version, do not bump it, and go to Step 3.
- Any other branch: ask the user one question, "Dev release or stable release?", and continue to Step 2.

A dev release pushes the branch `release/vX.X.X`. The workflow publishes the tag `vX.X.X-dev` as a GitHub pre-release.
A stable release pushes the tag `vX.X.X` on `main`. The workflow publishes the tag `vX.X.X` as a full release.

## Step 2: recommend a version

Skip this step on a `release/vX.X.X` branch; the branch fixes the version.

Read the current tags, the recent commits, and the open release branches.

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

Tell the user the latest stable tag, the reason for the recommendation, and the recommended version. Ask the user to confirm the version or give another version.

## Step 3: run the release

Run only the branch that matches the confirmed kind.

### Dev release

1. If the current branch is already `release/vX.X.X`, use it. Otherwise run `git checkout main`, `git pull --ff-only`, then `git checkout -b release/vX.X.X` for a new version, or `git checkout release/vX.X.X` for an existing branch.
2. Commit any outstanding work on the branch.
3. Confirm the push with the user one more time.
4. Run `git push -u origin release/vX.X.X`.
5. Do not wait for the workflow. Report the pushed branch and the tag `vX.X.X-dev`, then stop.

Every later push to the branch republishes `vX.X.X-dev`. The build keeps the numeric version `X.X.X`.

### Stable release

1. Confirm with the user that a stable release is wanted, and name the version in the question.
2. Run `git checkout main` and `git pull --ff-only`.
3. Run `git merge --no-ff release/vX.X.X`.
4. Add or update `docs/releases/vX.X.X.md` before the tag.
5. Confirm the push with the user one more time.
6. Run `git push origin main`, then `git tag vX.X.X`, then `git push origin vX.X.X`.
7. Do not wait for the workflow. Report the pushed tag `vX.X.X`, then stop.

## Notes

- The workflow file is `.github/workflows/release.yml`.
- The `prepare` job derives the version, the tag, and the pre-release flag from the git ref.
- The guard `if: needs.prepare.outputs.is_release == 'true'` keeps `workflow_dispatch` runs from publishing.
- Keep `docs/releases/<tag>.md` next to the release notes, because the publish job copies that file into the release body.
- Never sleep, poll, or watch a GitHub Actions run to wait for a release. Report the push and stop.
