{
  description = "Dev shell for building valgrind-codspeed";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          # Valgrind tool objects link -nodefaultlibs and run without glibc's %fs
          # TLS, so the toolchain must not inject stack-protector or fortify
          # (__*_chk) into them. The compiler wrapper otherwise re-adds these
          # over our -fno-stack-protector / -D_FORTIFY_SOURCE=0 flags. This
          # covers the vendored Capstone too, which is compiled into the tool.
          hardeningDisable = [
            "stackprotector"
            "fortify"
            "fortify3"
          ];

          packages = [
            pkgs.python3
            pkgs.uv
            pkgs.autoconf
            pkgs.automake
            pkgs.libtool
            pkgs.gnumake
            pkgs.gcc
            pkgs.pkg-config
          ];
        };
      }
    );
}
