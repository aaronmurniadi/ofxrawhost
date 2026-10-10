# Agent notes

## Release workflow

Two kinds of release exist. A dev release comes from a `release/vX.X.X` branch and
publishes the tag `vX.X.X-dev` as a GitHub pre-release. A stable release comes from
a `vX.X.X` tag on `main` and publishes the tag `vX.X.X` as a full release.

The workflow file is `.github/workflows/release.yml`. It reads the version from
the git ref. The `prepare` job sets the version, the tag, and the pre-release flag.

## Release rules

Never create a stable release unless the user asks for it in that request.
Never push a `vX.X.X` tag unless the user asks for it in that request.
Always ask the user for the release kind before any release action.
Always ask the user for the version number before you create a release branch.
Never git push unless the user asks for it in that request.
