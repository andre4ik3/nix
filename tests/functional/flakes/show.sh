#!/usr/bin/env bash

source ./common.sh

flakeDir=$TEST_ROOT/flake
mkdir -p "$flakeDir"

writeSimpleFlake "$flakeDir"
pushd "$flakeDir"


# By default: Only show the packages content for the current system and no
# legacyPackages at all
nix flake show --json > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.inventory.packages.output.children.someOtherSystem.filtered;
assert show_output.inventory.packages.output.children.${builtins.currentSystem}.children.default.derivation.name == "simple";
assert show_output.inventory.legacyPackages.output.children.${builtins.currentSystem}.isLegacy;
true
'

# `--eval-system` should control system selection for flake output traversal.
nix flake show --eval-system someOtherSystem --json > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.inventory.packages.output.children.${builtins.currentSystem}.filtered;
assert show_output.inventory.packages.output.children.someOtherSystem.children.default.derivation.name == "simple";
true
'

# With `--all-systems`, show the packages for all systems
nix flake show --json --all-systems > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.inventory.packages.output.children.someOtherSystem.children.default.derivation.name == "simple";
assert show_output.inventory.legacyPackages.output.children.${builtins.currentSystem}.isLegacy;
true
'

# With `--legacy`, show the legacy packages
nix flake show --json --legacy > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.inventory.legacyPackages.output.children.${builtins.currentSystem}.children.hello.derivation.name == "simple";
true
'

# Test that attributes with errors are handled correctly.
# nixpkgs.legacyPackages is a particularly prominent instance of this.
cat >flake.nix <<EOF
{
  outputs = inputs: {
    legacyPackages.$system = {
      AAAAAASomeThingsFailToEvaluate = throw "nooo";
      simple = import ./simple.nix;
    };
  };
}
EOF
nix flake show --json --legacy --all-systems > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.inventory.legacyPackages.output.children.${builtins.currentSystem}.children.AAAAAASomeThingsFailToEvaluate.failed;
assert show_output.inventory.legacyPackages.output.children.${builtins.currentSystem}.children.simple.derivation.name == "simple";
true
'

# Test that nix flake show doesn't fail if one of the outputs contains
# an IFD
popd
writeIfdFlake "$flakeDir"
pushd "$flakeDir"

[[ $(nix flake show --json | jq -r ".inventory.packages.output.children.\"$system\".children.default.derivation.name") = top ]]

cat >flake.nix<<EOF
{
  outputs = inputs: {
    packages.$system = {
      aNoDescription = import ./simple.nix;
      bOneLineDescription = import ./simple.nix // { meta.description = "one line"; };
      cMultiLineDescription = import ./simple.nix // { meta.description = ''
         line one
        line two
      ''; };
      dLongDescription = import ./simple.nix // { meta.description = ''
        abcdefghijklmnopqrstuvwxyz
      ''; };
      eEmptyDescription = import ./simple.nix // { meta.description = ""; };
    };
  };
}
EOF
unbuffer sh -c '
  stty rows 20 cols 100
  nix flake show --drv-names > show-output.txt
'
test "$(awk -F '[:] ' '/aNoDescription/{print $NF}' ./show-output.txt)" = "package 'simple'"
test "$(awk -F '[:] ' '/bOneLineDescription/{print $NF}' ./show-output.txt)" = "package 'simple' - 'one line'"
test "$(awk -F '[:] ' '/cMultiLineDescription/{print $NF}' ./show-output.txt)" = "package 'simple' - 'line one'"
test "$(awk -F '[:] ' '/dLongDescription/{print $NF}' ./show-output.txt)" = "package 'simple' - 'abcdefghijklmnopqrs...'"
test "$(awk -F '[:] ' '/eEmptyDescription/{print $NF}' ./show-output.txt)" = "package 'simple'"
