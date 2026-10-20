# Use the caller's Nixpkgs while retaining the fork's dependency overrides.
{ pkgs }:

let
  lock = builtins.fromJSON (builtins.readFile ./flake.lock);
  packageSets = import ./packaging/package-sets.nix {
    inherit pkgs;
    inherit (pkgs) lib;
    src = ./.;
    officialRelease = true;
    inputs.bdwgc = builtins.fetchTree lock.nodes.bdwgc.locked;
  };
in
packageSets.nixComponents
