args@{
  lib,
  pkgs,
  inputs,
  src,
  officialRelease,
  getStdenv ? pkgs: pkgs.stdenv,
}:

let
  # Nixpkgs and makeScopeWithSplicing' use different splice prefixes.
  renameSplicesFrom = prefix: x: {
    buildBuild = x."${prefix}BuildBuild";
    buildHost = x."${prefix}BuildHost";
    buildTarget = x."${prefix}BuildTarget";
    hostHost = x."${prefix}HostHost";
    hostTarget = x."${prefix}HostTarget";
    targetTarget = x."${prefix}TargetTarget";
  };

  renameSplicesTo = prefix: x: {
    "${prefix}BuildBuild" = x.buildBuild;
    "${prefix}BuildHost" = x.buildHost;
    "${prefix}BuildTarget" = x.buildTarget;
    "${prefix}HostHost" = x.hostHost;
    "${prefix}HostTarget" = x.hostTarget;
    "${prefix}TargetTarget" = x.targetTarget;
  };

  mapSplices =
    f:
    {
      buildBuild,
      buildHost,
      buildTarget,
      hostHost,
      hostTarget,
      targetTarget,
    }:
    {
      buildBuild = f buildBuild;
      buildHost = f buildHost;
      buildTarget = f buildTarget;
      hostHost = f hostHost;
      hostTarget = f hostTarget;
      targetTarget = f targetTarget;
    };

  packageSetsFor = args: import ./package-sets.nix args;

  nixComponentsSplices = mapSplices (
    pkgs': (packageSetsFor (args // { pkgs = pkgs'; })).nixComponents
  ) (renameSplicesFrom "pkgs" pkgs);
  nixDependenciesSplices = mapSplices (
    pkgs': (packageSetsFor (args // { pkgs = pkgs'; })).nixDependencies
  ) (renameSplicesFrom "pkgs" pkgs);

  # Keep patched dependencies local to Nix, rather than changing the caller's pkgs.
  nixDependencies =
    lib.makeScopeWithSplicing'
      {
        inherit (pkgs) splicePackages newScope;
      }
      {
        otherSplices = renameSplicesTo "self" nixDependenciesSplices;
        f = import ./dependencies.nix {
          inherit inputs pkgs;
          stdenv = getStdenv pkgs;
        };
      };

  nixComponents =
    lib.makeScopeWithSplicing'
      {
        inherit (pkgs) splicePackages;
        inherit (nixDependencies) newScope;
      }
      {
        otherSplices = renameSplicesTo "self" nixComponentsSplices;
        f = import ./components.nix {
          inherit
            lib
            pkgs
            src
            officialRelease
            ;
          maintainers = [ ];
        };
      };

  # Target-only splices may lack a complete stdenv, as in other Nixpkgs scopes.
  fixup = lib.mapAttrs (k: v: if !(pkgs ? nix) then { } else v);
in
fixup {
  inherit nixDependencies nixComponents;
}
