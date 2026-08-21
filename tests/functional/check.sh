#!/usr/bin/env bash

source common.sh

# XXX: This shouldn’t be, but #4813 cause this test to fail
buggyNeedLocalStore "see #4813"

checkBuildTempDirRemoved ()
{
    buildDir=$(sed -n 's/CHECK_TMPDIR=//p' "$1" | head -1)
    checkBuildIdFile=${buildDir}/checkBuildId
    [[ ! -f $checkBuildIdFile ]] || ! grep "$checkBuildId" "$checkBuildIdFile"
}

# written to build temp directories to verify created by this instance
checkBuildId=$(date +%s%N)

TODO_NixOS

nix-build dependencies.nix --no-out-link
nix-build dependencies.nix --no-out-link --check

# Make sure checking just one output works (#13293)
nix-build multiple-outputs.nix -A a --no-out-link
nix-store --delete "$(nix-build multiple-outputs.nix -A a.second --no-out-link)"
nix-build multiple-outputs.nix -A a.first --no-out-link --check

# Build failure exit codes (100, 104, etc.) are from
# doc/manual/source/command-ref/status-build-failure.md

# check for dangling temporary build directories
# only retain if build fails and --keep-failed is specified, or...
# ...build is non-deterministic and --check and --keep-failed are both specified
nix-build check.nix -A failed --argstr checkBuildId "$checkBuildId" \
    --no-out-link 2> "$TEST_ROOT/log" || status=$?
[ "$status" = "100" ]
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A failed --argstr checkBuildId "$checkBuildId" \
    --no-out-link --keep-failed 2> "$TEST_ROOT/log" || status=$?
[ "$status" = "100" ]
if checkBuildTempDirRemoved "$TEST_ROOT/log"; then false; fi

test_custom_build_dir() {
  local customBuildDir="$TEST_ROOT/custom-build-dir"

  nix-build check.nix -A failed --argstr checkBuildId "$checkBuildId" \
      --no-out-link --keep-failed --option build-dir "$TEST_ROOT/custom-build-dir" 2> "$TEST_ROOT/log" || status=$?
  [ "$status" = "100" ]
  [[ 1 == "$(count "$customBuildDir/"*)" ]]
  local buildDir=("$customBuildDir/"*)
  if [[ "${#buildDir[@]}" -ne 1 ]]; then
    echo "expected one build directory, got: ${buildDir[*]}" >&2
    exit 1
  fi
  if [[ -e ${buildDir[*]}/build ]]; then
      buildDir[0]="${buildDir[*]}/build"
  fi
  grep "$checkBuildId" "${buildDir[*]}/checkBuildId"
}
test_custom_build_dir

test_custom_temp_dir() {
  # Like test_custom_build_dir(), but configure temp-dir instead.
  local customTempDir="$TEST_ROOT/custom-temp-dir"

  mkdir "$customTempDir"
  local status=
  nix-build check.nix -A failed --argstr checkBuildId "$checkBuildId" \
      --no-out-link --keep-failed --option temp-dir "$customTempDir" 2> "$TEST_ROOT/log" || status=$?
  [ "$status" = "100" ]
  # Don't assert a specific temp dir prefix; only location is guaranteed.
  local buildDir
  buildDir=$(sed -n 's/CHECK_TMPDIR=//p' "$TEST_ROOT/log" | head -1)
  [[ $buildDir = "$customTempDir"/* ]]
  if [[ -e "$buildDir/build" ]]; then
      buildDir="$buildDir/build"
  fi
  grep "$checkBuildId" "$buildDir/checkBuildId"

  # Also check a non-build-dir code path: nix-shell rcfile temp path.
  local rcpath
  # shellcheck disable=SC2016 # $0 must expand inside the spawned nix-shell.
  rcpath=$(NIX_BUILD_SHELL=$SHELL nix-shell check.nix -A deterministic --option temp-dir "$customTempDir" --run 'echo $0' 2> "$TEST_ROOT/log")
  [[ $rcpath = "$customTempDir"/* ]]
}
test_custom_temp_dir

test_shell_preserves_tmpdir_root() {
  # Ensure interactive-shell commands use the environment's TMPDIR rather than temp-dir.
  local envTempDir="$TEST_ROOT/shell-temp-dir-env"
  mkdir "$envTempDir"
  local settingTempDir="$TEST_ROOT/shell-temp-dir-setting"
  mkdir "$settingTempDir"

  # shellcheck disable=SC2016 # $out is Nix code, not shell expansion.
  local expr='with import ./config.nix; mkDerivation { name = "foo"; buildCommand = "echo foo > $out"; outputs = [ "out" ]; }'

  local output
  # shellcheck disable=SC2016 # $TMPDIR must expand in the command shell.
  output=$(TMPDIR="$envTempDir" NIX_BUILD_SHELL=$SHELL nix-shell -E "$expr" --option temp-dir "$settingTempDir" --command 'echo $TMPDIR' 2> "$TEST_ROOT/log")
  [[ $output = "$envTempDir"/nix-shell-* ]]
  [[ ! -e $output ]]

  # shellcheck disable=SC2016 # $TMPDIR must expand in the command shell.
  output=$(TMPDIR="$envTempDir" nix develop --impure -E "$expr" --option temp-dir "$settingTempDir" --command bash -c 'echo $TMPDIR' 2> "$TEST_ROOT/log" || true)
  [[ -z $output || $output != "$settingTempDir"/* ]]

  # shellcheck disable=SC2016 # $TMPDIR must expand in the command shell.
  output=$(TMPDIR="$envTempDir" nix shell --impure -E "$expr" --option temp-dir "$settingTempDir" --command bash -c 'echo $TMPDIR' 2> "$TEST_ROOT/log" || true)
  [[ -z $output || $output != "$settingTempDir"/* ]]
}
test_shell_preserves_tmpdir_root

nix-build check.nix -A deterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link 2> "$TEST_ROOT/log"
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A deterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link --check --keep-failed 2> "$TEST_ROOT/log"
if grepQuiet 'may not be deterministic' "$TEST_ROOT/log"; then false; fi
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A nondeterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link 2> "$TEST_ROOT/log"
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A nondeterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link --check 2> "$TEST_ROOT/log" || status=$?
grep 'may not be deterministic' "$TEST_ROOT/log"
# both outputs should be reported as differing
[[ $(grep -c 'differs' "$TEST_ROOT/log") = 2 ]]
[ "$status" = "104" ]
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A nondeterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link --check --keep-failed 2> "$TEST_ROOT/log" || status=$?
grep 'may not be deterministic' "$TEST_ROOT/log"
[ "$status" = "104" ]
if checkBuildTempDirRemoved "$TEST_ROOT/log"; then false; fi

TODO_NixOS

clearStore

path=$(nix-build check.nix -A fetchurl --no-out-link)

chmod +w "$path"
echo foo > "$path"
chmod -w "$path"

nix-build check.nix -A fetchurl --no-out-link --check
# Note: "check" doesn't repair anything, it just compares to the hash stored in the database.
[[ $(cat "$path") = foo ]]

nix-build check.nix -A fetchurl --no-out-link --repair
[[ $(cat "$path") != foo ]]

echo 'Hello World' > "$TEST_ROOT/dummy"
nix-build check.nix -A hashmismatch --no-out-link || status=$?
[ "$status" = "102" ]

echo -n > "$TEST_ROOT/dummy"
nix-build check.nix -A hashmismatch --no-out-link
echo 'Hello World' > "$TEST_ROOT/dummy"

nix-build check.nix -A hashmismatch --no-out-link --check || status=$?
[ "$status" = "102" ]

# Multiple failures with --keep-going
nix-build check.nix -A nondeterministic --no-out-link
nix-build check.nix -A nondeterministic -A hashmismatch --no-out-link --check --keep-going || status=$?
[ "$status" = "110" ]
