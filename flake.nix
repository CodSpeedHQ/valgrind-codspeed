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

        # Capstone for the Callgrind cycle-estimation decoder, built the same way
        # as the CI workflows (see .github/workflows): x86 + arm64 only, static.
        #
        # Valgrind tools run without glibc's %fs TLS and link -nodefaultlibs, so
        # Capstone must be built without stack-protector (its %fs:0x28 canary
        # read faults at runtime) and without fortify (pulls __*_chk libc
        # symbols); hardeningDisable drops both. Limiting the architectures also
        # drops the non-x86/arm64 instruction printers that reference libc
        # symbols (e.g. XCore's strtol) the tool does not shim.
        capstone = pkgs.capstone.overrideAttrs (old: {
          cmakeFlags = (old.cmakeFlags or [ ]) ++ [
            "-DCAPSTONE_ARCHITECTURE_DEFAULT=OFF"
            "-DCAPSTONE_X86_SUPPORT=ON"
            "-DCAPSTONE_ARM64_SUPPORT=ON"
          ];
          hardeningDisable = (old.hardeningDisable or [ ]) ++ [
            "stackprotector"
            "fortify"
            "fortify3"
          ];
        });
      in
      {
        # Expose the pinned Capstone so the autotools build and scripts can find
        # it via `nix build .#capstone` or the CAPSTONE_DIR env var below.
        packages.capstone = capstone;

        devShells.default = pkgs.mkShell {
          # Valgrind tool objects link -nodefaultlibs and run without glibc's %fs
          # TLS, so the toolchain must not inject stack-protector or fortify
          # (__*_chk) into them. The compiler wrapper otherwise re-adds these
          # over our -fno-stack-protector / -D_FORTIFY_SOURCE=0 flags.
          hardeningDisable = [
            "stackprotector"
            "fortify"
            "fortify3"
          ];

          packages = [
            capstone
            pkgs.python3
            pkgs.uv
            pkgs.autoconf
            pkgs.automake
            pkgs.libtool
            pkgs.gnumake
            pkgs.gcc
            pkgs.pkg-config
          ];

          # Consumed by configure (--with-capstone), the LUT generator, and the
          # standalone cycledecode test. Point them at the hardening-free build.
          CAPSTONE_DIR = "${capstone}";
        };
      }
    );
}
