#!/usr/bin/env bash

source common.sh

TODO_NixOS

drvPath=$(nix-instantiate dependencies.nix)
outPath=$(nix-store -rvv "$drvPath")

# Set a GC root.
rm -f "$NIX_STATE_DIR/gcroots/foo"
ln -sf "$outPath" "$NIX_STATE_DIR/gcroots/foo"

expectStderr 0 nix-store -q --roots "$outPath" | grepQuiet "$NIX_STATE_DIR/gcroots/foo -> $outPath"

nix-store --gc --print-roots | grep "$outPath"
nix-store --gc --print-live | grep "$outPath"
nix-store --gc --print-dead | grep "$drvPath"
if nix-store --gc --print-dead | grep -E "$outPath"$; then false; fi

nix-store --gc --print-dead

inUse=$(readLink "$outPath/reference-to-input-2")
expectStderr 1 nix-store --delete "$inUse" | grepQuiet "Cannot delete some of the given paths because they are still alive"
test -e "$inUse"

expectStderr 1 nix-store --delete "$outPath" | grepQuiet "Cannot delete some of the given paths because they are still alive"
test -e "$outPath"

for i in "$NIX_STORE_DIR"/*; do
    if [[ $i =~ /trash ]]; then continue; fi # compat with old daemon
    touch "$i.lock"
    touch "$i.chroot"
done

nix-collect-garbage

# Check that the root and its dependencies haven't been deleted.
cat "$outPath/foobar"
cat "$outPath/reference-to-input-2/bar"

# Check that the derivation has been GC'd.
if test -e "$drvPath"; then false; fi

rm "$NIX_STATE_DIR/gcroots/foo"

# Deleting a closure should report its combined statistics correctly.
mapfile -t closure < <(nix-store -qR "$outPath")
nix-store --delete "${closure[@]}" > delete-output
grepQuiet -E "^[1-9][0-9]* store paths deleted, 10[0-9][.][0-9] KiB freed$" delete-output
test -z "$(grep "0 paths deleted" delete-output)"

nix-collect-garbage

# Check that the output has been GC'd.
if test -e "$outPath/foobar"; then false; fi

# Check that the store is empty.
rmdir "$NIX_STORE_DIR/.links"
rmdir "$NIX_STORE_DIR"
