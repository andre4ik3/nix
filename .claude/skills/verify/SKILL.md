---
name: verify
description: Build Nix and exercise CLI changes through the built binary.
---

# Verify Nix CLI changes

Build the requested platform's default package:

```sh
nix build .#packages.<system>.default --no-link --print-out-paths --print-build-logs
```

For REPL interruption changes, drive the built CLI through the functional Expect script:

```sh
NIX_BIN=/nix/store/<output>/bin/nix "$(type -P expect)" tests/functional/repl-interrupt.exp
```

Flake source snapshots omit untracked files. Add new source or test files to the Git index before building.
