#!/usr/bin/env bash

source common.sh

resultPath="$TEST_ROOT/result-store-delete-unlink"
outPath="$(nix-build dependencies.nix -o "$resultPath")"

test -L "$resultPath"
test -e "$outPath"

# The root keeps this path alive, so delete should fail.
expect 1 nix store delete "$resultPath"
test -L "$resultPath"
test -e "$outPath"

# --unlink should remove the root and let the deletion proceed.
nix store delete --unlink "$resultPath"
test ! -e "$resultPath"
test ! -e "$outPath"

resultPath="$TEST_ROOT/result-store-delete-unlink-closure"
outPath="$(nix-build dependencies.nix -o "$resultPath")"
input2="$(readlink -f "$outPath/reference-to-input-2")"
input0="$(cat "$input2/input0")"

test -L "$resultPath"
test -e "$outPath"
test -e "$input2"
test -e "$input0"

nix store delete --unlink --delete-closure "$resultPath"
test ! -e "$resultPath"
test ! -e "$outPath"
test ! -e "$input2"
test ! -e "$input0"
