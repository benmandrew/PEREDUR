{
  description = "PEREDUR — genetic repair of FRETISH specifications";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # ctcache (matus-chochlik/ctcache) — result cache for clang-tidy. Its
        # client is a single stdlib-only Python script (server-only extras like
        # flask/redis are lazily imported and unused in local CTCACHE_DIR mode),
        # so we vendor just that script and expose it on PATH as
        # `clang-tidy-cache`. Being on PATH is what turns the lint path's
        # caching on (see cmake/run_clang_tidy.cmake), which by default stores
        # results under the build directory rather than ctcache's own /tmp
        # default.
        ctcacheSrc = pkgs.fetchFromGitHub {
          owner = "matus-chochlik";
          repo = "ctcache";
          rev = "1.2.0";
          sha256 = "1h6kj9cycn1a0i4bv1g0pbw53lqwl4vf920vyzyxh5ij8acaff0y";
        };
        clang-tidy-cache = pkgs.writeShellScriptBin "clang-tidy-cache" ''
          exec ${pkgs.python3}/bin/python3 \
            ${ctcacheSrc}/src/ctcache/clang_tidy_cache.py "$@"
        '';
      in {
        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            # clang-tidy result cache (used whenever present; see above)
            clang-tidy-cache

            # Core build system
            cmake
            ninja
            gnumake
            gcc
            pkg-config

            # FetchContent / ExternalProject: git clones and HTTPS downloads
            git
            curl
            cacert

            # cpptrace: libunwind-based stack unwinding (CPPTRACE_UNWIND_WITH_LIBUNWIND=ON)
            libunwind

            # Lint targets (clang-tidy, clang-format, cppcheck, cpplint)
            clang-tools
            cppcheck
            cpplint

            # fuzz/ltl_equivalence_fuzzer (PEREDUR_FUZZ): -fsanitize=fuzzer
            # (libFuzzer) is a clang/compiler-rt feature with no GCC
            # equivalent, so the fuzz target is built with its own clang++
            # invocation (see fuzz/CMakeLists.txt) rather than the project's
            # normal GCC-based build.
            clang

            # Coverage (`cmake --workflow --preset coverage`, then
            # scripts/coverage_badge.py): llvm-profdata merges the profiles the
            # instrumented suite writes and llvm-cov reads them back. Both must
            # be the same LLVM release as the clang above, since the profile
            # format is versioned -- an older llvm-profdata reports a current
            # profile as malformed rather than as out of date.
            llvmPackages.llvm

            # Docs targets (doxygen + sphinx)
            doxygen
            (python3.withPackages (ps: with ps; [
              sphinx
              breathe
              furo
              sphinxcontrib-katex
            ]))

            # KaTeX renders the maths in doc comments. The extension above
            # bundles the JS but not the stylesheet, whose upstream default is a
            # CDN URL; cmake/docs.cmake copies katex.min.css and its fonts out of
            # this package so the built site fetches nothing at view time.
            katex

            # SMT backend for building black-sat from source (cmake/black.cmake's
            # fallback path on non-Ubuntu-24.04 hosts, where the prebuilt deb doesn't apply).
            z3

            # Runtime dependency of the vendored FRET formaliser CLI
            # (vendor/fretCLI.main.js, see runner/formaliser.hpp): `node` is
            # looked up on PATH at run time, not fetched by CMake.
            nodejs
          ]
          # Runtime dependency of the prebuilt black-sat binary, which
          # dynamically links libfmt.so.9 and does not bundle it. Only the
          # Linux path of cmake/black.cmake uses that binary — elsewhere black
          # is built from source against z3 — and fmt 9.1.0's test suite does
          # not compile under clang 21 (it specialises std::is_floating_point),
          # so pulling it in on darwin breaks the shell for a file nothing reads.
          ++ lib.optional stdenv.hostPlatform.isLinux fmt_9;

          # Point curl/git at the nix CA bundle — required on NixOS, harmless on Ubuntu
          SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
          GIT_SSL_CAINFO = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
          CURL_CA_BUNDLE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";

          # Deliberately not LD_LIBRARY_PATH: that leaks into every child
          # process launched from a shell with this devShell active (e.g.
          # `code .`), silently swapping in nix-store libs for unrelated
          # programs and crashing them on ABI mismatches. cmake/black.cmake
          # reads this plain var and bakes the path directly into the
          # black-sat wrapper script instead, scoped to just that one exec.
          PEREDUR_FMT9_LIB_DIR =
            pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isLinux
              "${pkgs.fmt_9}/lib";
        };
      });
}
