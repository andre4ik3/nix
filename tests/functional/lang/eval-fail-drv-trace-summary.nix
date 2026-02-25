let
  transitive-dependency = derivation {
    name = "transitive-dependency";
    system = "x86_64-linux";
    builder = "/bin/sh";
    __structuredAttrs = true;
    nativeBuildInputs = [
      null
      (throw "transitive dependency growls")
    ];
  };

  direct-dependency = derivation {
    name = "direct-dependency";
    system = "x86_64-linux";
    builder = "/bin/sh";
    __structuredAttrs = true;
    libtrans = transitive-dependency;
  };

  package-you-care-about = derivation {
    name = "package-you-care-about";
    system = "x86_64-linux";
    builder = "/bin/sh";
    __structuredAttrs = true;
    buildInputs = [
      direct-dependency
    ];
  };
in
package-you-care-about.outPath
