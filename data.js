window.BENCHMARK_DATA = {
  "lastUpdate": 1789804764816,
  "repoUrl": "https://github.com/benmandrew/PEREDUR",
  "entries": {
    "counter benchmarks": [
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "323680e4600b389ab27b8cdc0ba118589064e84e",
          "message": "feat: add descriptive names to benchmarks via ->Name()\n\nReplaces raw function names with human-readable labels on the\ngithub-action-benchmark GitHub Pages charts.",
          "timestamp": "2026-06-26T14:34:26+01:00",
          "tree_id": "f2bdba9700852bcf5ff64463782dd3f3cfe5a26f",
          "url": "https://github.com/benmandrew/counter/commit/323680e4600b389ab27b8cdc0ba118589064e84e"
        },
        "date": 1782481406688,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 656.0685095423507,
            "unit": "ns/iter",
            "extra": "iterations: 1086418\ncpu: 655.9967857675408 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2428.0910232418983,
            "unit": "ns/iter",
            "extra": "iterations: 285905\ncpu: 2427.8745527360497 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 909.3566041392346,
            "unit": "ns/iter",
            "extra": "iterations: 763718\ncpu: 909.2056858683441 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 209.20706841668914,
            "unit": "ns/iter",
            "extra": "iterations: 3437743\ncpu: 209.13034278594992 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 230.07660938691873,
            "unit": "ns/iter",
            "extra": "iterations: 3043426\ncpu: 230.0220057921566 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 256.4907849008149,
            "unit": "ns/iter",
            "extra": "iterations: 2726612\ncpu: 256.4306751382302 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 310.0372107726432,
            "unit": "ns/iter",
            "extra": "iterations: 2258647\ncpu: 309.9792397838177 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3909.0007865188286,
            "unit": "ns/iter",
            "extra": "iterations: 179271\ncpu: 3908.265592315545 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "5c8702760acd914661f346d570cab68e2f3234ae",
          "message": "fix: enable sccache GHA cache backend via SCCACHE_GHA_ENABLED",
          "timestamp": "2026-06-26T14:45:07+01:00",
          "tree_id": "be70eba8d15a7a496f6ef02c5bd58ce500e038d5",
          "url": "https://github.com/benmandrew/counter/commit/5c8702760acd914661f346d570cab68e2f3234ae"
        },
        "date": 1782481804341,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 739.0545446401281,
            "unit": "ns/iter",
            "extra": "iterations: 959966\ncpu: 739.0531539658698 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2617.908086817267,
            "unit": "ns/iter",
            "extra": "iterations: 269624\ncpu: 2617.744993027327 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 870.8529666928075,
            "unit": "ns/iter",
            "extra": "iterations: 799948\ncpu: 870.8482038832527 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 206.16903479322,
            "unit": "ns/iter",
            "extra": "iterations: 3395863\ncpu: 206.11824799763716 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 239.95250681336313,
            "unit": "ns/iter",
            "extra": "iterations: 2917429\ncpu: 239.8988468956742 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 262.73857660538596,
            "unit": "ns/iter",
            "extra": "iterations: 2696090\ncpu: 262.6764158466519 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 322.5050580551939,
            "unit": "ns/iter",
            "extra": "iterations: 2170004\ncpu: 322.44272222539684 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3812.6988788625044,
            "unit": "ns/iter",
            "extra": "iterations: 182850\ncpu: 3811.966086956521 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "49b2eee19ae9a0d75afec375990aa40b46aca50e",
          "message": "ci: gate build-and-test on check passing",
          "timestamp": "2026-06-26T14:48:21+01:00",
          "tree_id": "5c220f8d54222ebf0ad47b5f037503f92da6cf8e",
          "url": "https://github.com/benmandrew/counter/commit/49b2eee19ae9a0d75afec375990aa40b46aca50e"
        },
        "date": 1782481949327,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 641.2142994786103,
            "unit": "ns/iter",
            "extra": "iterations: 1089774\ncpu: 641.1871149430984 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2451.1241264675755,
            "unit": "ns/iter",
            "extra": "iterations: 285765\ncpu: 2450.9508897170754 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 903.0263139660816,
            "unit": "ns/iter",
            "extra": "iterations: 750476\ncpu: 903.0102415000613 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 204.2399973242386,
            "unit": "ns/iter",
            "extra": "iterations: 3453226\ncpu: 204.22263964188852 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 231.10704484782778,
            "unit": "ns/iter",
            "extra": "iterations: 3052188\ncpu: 231.09693767225355 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 256.0898837757512,
            "unit": "ns/iter",
            "extra": "iterations: 2715440\ncpu: 256.0586906725983 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 310.6386689226117,
            "unit": "ns/iter",
            "extra": "iterations: 2262468\ncpu: 310.6177285159394 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3893.066275643462,
            "unit": "ns/iter",
            "extra": "iterations: 179870\ncpu: 3892.477533774393 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "facb19a7eb443e765487648bc836fdbb30dd2c92",
          "message": "fix: propagate worker thread exceptions to main thread instead of terminating\n\nAn uncaught exception in ThreadPool::worker_loop (e.g. from ganak or black\nreturning a non-zero exit code) caused std::terminate -> SIGABRT, crashing\nthe whole run with no useful error message. Store exceptions in the promise\nso they surface on future::get() in the main thread, then catch them in\nmain() and exit cleanly with a fatal: <message> diagnostic.",
          "timestamp": "2026-06-26T15:13:29+01:00",
          "tree_id": "2f092f01c1780bcc13c9cfca9a336fd3999e5ab4",
          "url": "https://github.com/benmandrew/counter/commit/facb19a7eb443e765487648bc836fdbb30dd2c92"
        },
        "date": 1782495311227,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 682.2087287003706,
            "unit": "ns/iter",
            "extra": "iterations: 996437\ncpu: 682.0727110695408 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2445.482755954507,
            "unit": "ns/iter",
            "extra": "iterations: 284533\ncpu: 2445.2326057083014 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 865.8476959967734,
            "unit": "ns/iter",
            "extra": "iterations: 800281\ncpu: 865.7845431792082 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 206.2405216362074,
            "unit": "ns/iter",
            "extra": "iterations: 3391329\ncpu: 206.22404785852387 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 233.10898449399295,
            "unit": "ns/iter",
            "extra": "iterations: 3003675\ncpu: 233.0721263119346 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 259.9794118913435,
            "unit": "ns/iter",
            "extra": "iterations: 2694128\ncpu: 259.9687572379633 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 312.8954411672585,
            "unit": "ns/iter",
            "extra": "iterations: 2235660\ncpu: 312.85461250816314 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3875.8243723046703,
            "unit": "ns/iter",
            "extra": "iterations: 181099\ncpu: 3875.6528583813288 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "78f9091b87c1f24fac147054e0158c571f02a2d4",
          "message": "feat: add Nix dev shell as primary development workflow\n\nAdds flake.nix providing cmake, make, gcc, git, curl, libunwind, and\nlint tools (clang-tidy, clang-format, cppcheck, cpplint). CA bundle env\nvars ensure HTTPS downloads work on NixOS. SPOT, ganak, and black-sat\ncontinue to be fetched/built by CMake at configure time.\n\nUpdates README and CLAUDE.md to present `nix develop` as the\nfirst-class entry point, with the non-Nix path documented as supported\nbut secondary.",
          "timestamp": "2026-07-03T16:23:28+01:00",
          "tree_id": "af38aabdd586c39248127dffa3a6d8151a5c9447",
          "url": "https://github.com/benmandrew/counter/commit/78f9091b87c1f24fac147054e0158c571f02a2d4"
        },
        "date": 1783092449584,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 648.3040554837355,
            "unit": "ns/iter",
            "extra": "iterations: 1082263\ncpu: 648.2735268599222 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2463.5509720628306,
            "unit": "ns/iter",
            "extra": "iterations: 285784\ncpu: 2463.438862917448 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 905.6133488355589,
            "unit": "ns/iter",
            "extra": "iterations: 760321\ncpu: 905.5480093276394 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 203.21336534099433,
            "unit": "ns/iter",
            "extra": "iterations: 3436261\ncpu: 203.1921259764611 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 230.2990312306697,
            "unit": "ns/iter",
            "extra": "iterations: 2919374\ncpu: 230.23883579150873 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 257.1070654230573,
            "unit": "ns/iter",
            "extra": "iterations: 2724437\ncpu: 257.0466734962123 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 309.7405745798356,
            "unit": "ns/iter",
            "extra": "iterations: 2257963\ncpu: 309.66705876048485 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3930.9674294688944,
            "unit": "ns/iter",
            "extra": "iterations: 178290\ncpu: 3930.034903808402 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "2340b3d507ca2007f8a4ff34f94704e596c0d0fa",
          "message": "fix: stop leaking Nix dev shell LD_LIBRARY_PATH into unrelated processes\n\nExporting LD_LIBRARY_PATH shell-wide affects every child process launched\nfrom a terminal with this devShell active (via direnv's `use flake`),\nnot just build/test commands. This silently swapped a mismatched\nnix-store libgcc_s.so.1 into VS Code when launched as `code .` from the\nproject directory, crashing it with SIGTRAP.\n\nstdenv.cc.cc.lib turned out to be unnecessary: nix's gcc-wrapper already\nbakes a correct RPATH for its own libstdc++/libgcc_s automatically, since\nCMake treats nix store paths as non-system and includes them (verified by\nrunning the debug test binary with LD_LIBRARY_PATH explicitly unset).\n\nThe only genuine runtime need was fmt_9, for the prebuilt black-sat .deb's\nwrapper script. That's now passed via a plain, inert env var\n(COUNTER_FMT9_LIB_DIR) and baked directly into the wrapper at configure\ntime, scoped to just that one exec instead of the whole shell.",
          "timestamp": "2026-07-06T14:30:29+01:00",
          "tree_id": "38a2c41b6dd69ce616449c32af1b51d5e89aaae4",
          "url": "https://github.com/benmandrew/counter/commit/2340b3d507ca2007f8a4ff34f94704e596c0d0fa"
        },
        "date": 1783346502391,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 526.3234429866371,
            "unit": "ns/iter",
            "extra": "iterations: 1336758\ncpu: 526.2670266420698 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2206.154235493687,
            "unit": "ns/iter",
            "extra": "iterations: 319278\ncpu: 2205.778083676295 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 877.7766466762449,
            "unit": "ns/iter",
            "extra": "iterations: 790380\ncpu: 877.7469672815605 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 129.25718770714042,
            "unit": "ns/iter",
            "extra": "iterations: 5420609\ncpu: 129.2353774271489 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 146.47547406727537,
            "unit": "ns/iter",
            "extra": "iterations: 4783182\ncpu: 146.4542687273868 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 164.54492556491326,
            "unit": "ns/iter",
            "extra": "iterations: 4241950\ncpu: 164.52736335883267 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 199.02178635796105,
            "unit": "ns/iter",
            "extra": "iterations: 3517660\ncpu: 198.96386944730284 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3751.9125979699556,
            "unit": "ns/iter",
            "extra": "iterations: 186792\ncpu: 3750.591770525507 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "a57c90fc050a4a85cf2f76822e225942569395ed",
          "message": "fix: stop CI cache hits from being defeated by ExternalProject rebuilds\n\nExternalProject_Add regenerates its per-step driver scripts on every\nconfigure. When a restored CI cache already contains the fully-built\nSPOT/black-sat third_party trees, those regenerated scripts get a newer\nmtime than the cached \"-done\" stamps, so Make reruns the whole\nconfigure/build/install anyway. Diagnosed via gh run logs: SPOT's ~9-13\nminute from-source rebuild was repeating on every one of the 5 CI matrix\nlegs regardless of cache hits, wasting ~45-65 CI-minutes per run.\n\nSkip re-declaring the ExternalProject_Add entirely (replacing it with a\nno-op custom target) whenever a prior run's \"-done\" stamp is already\npresent, for spot_project and black.cmake's three from-source sub-builds\n(tsl-hopscotch-map, nlohmann_json, black itself).\n\nSeparately, sccache was already wired up via CMAKE_C/CXX_COMPILER_LAUNCHER\nfor the main project, but never reached these ExternalProject sub-builds:\nSPOT's autotools ./configure picks its own CXX with no override, and the\nblack.cmake CMAKE_ARGS never passed the launcher through. Both now thread\nCMAKE_CXX_COMPILER_LAUNCHER in explicitly, so a genuine cache miss at\nleast compiles fast on a subsequent run.",
          "timestamp": "2026-07-06T14:50:01+01:00",
          "tree_id": "50d1b79eca2cedd33f9b6b79e958c503388b9d95",
          "url": "https://github.com/benmandrew/counter/commit/a57c90fc050a4a85cf2f76822e225942569395ed"
        },
        "date": 1783346621006,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 526.9278018505942,
            "unit": "ns/iter",
            "extra": "iterations: 1329300\ncpu: 526.8849040848567 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2192.7569700224003,
            "unit": "ns/iter",
            "extra": "iterations: 318471\ncpu: 2192.5653136392325 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 860.7856988033025,
            "unit": "ns/iter",
            "extra": "iterations: 793528\ncpu: 860.7171139518705 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 129.22128696150554,
            "unit": "ns/iter",
            "extra": "iterations: 5415127\ncpu: 129.21247701115786 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 146.56330831251387,
            "unit": "ns/iter",
            "extra": "iterations: 4773757\ncpu: 146.55154943999028 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 165.314684405103,
            "unit": "ns/iter",
            "extra": "iterations: 4259226\ncpu: 165.30165926860894 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 199.27061864896578,
            "unit": "ns/iter",
            "extra": "iterations: 3519702\ncpu: 199.2546226924892 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3789.6457190715796,
            "unit": "ns/iter",
            "extra": "iterations: 185065\ncpu: 3789.1533353146187 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "6a6b6a3b78e4abb015db5161daca0f245651c683",
          "message": "Merge pull request #5 from benmandrew/feature/ltl-formaliser-cli\n\nfeat: persistent LTL-formaliser CLI runner + FRETish-valid Requirement::to_string",
          "timestamp": "2026-07-07T11:52:41+01:00",
          "tree_id": "6d720b9de08bb38f78e58c408830b243cfacda29",
          "url": "https://github.com/benmandrew/counter/commit/6a6b6a3b78e4abb015db5161daca0f245651c683"
        },
        "date": 1783422515459,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 545.6766827054872,
            "unit": "ns/iter",
            "extra": "iterations: 1276486\ncpu: 545.6507936632286 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2234.4025531696393,
            "unit": "ns/iter",
            "extra": "iterations: 312396\ncpu: 2234.1677390235473 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 848.866899289286,
            "unit": "ns/iter",
            "extra": "iterations: 818493\ncpu: 848.7662154716048 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 162.27166654742433,
            "unit": "ns/iter",
            "extra": "iterations: 4332970\ncpu: 162.26006688253088 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 183.83791029239694,
            "unit": "ns/iter",
            "extra": "iterations: 3802197\ncpu: 183.80739477728264 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 211.95832134567257,
            "unit": "ns/iter",
            "extra": "iterations: 3361097\ncpu: 211.91618212744228 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 251.2760536532803,
            "unit": "ns/iter",
            "extra": "iterations: 2780445\ncpu: 251.23647977212272 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3783.088246564643,
            "unit": "ns/iter",
            "extra": "iterations: 187894\ncpu: 3782.755053381162 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "cad434aed8e5b9f81e6688d8378c6a40fd4934b5",
          "message": "feat: add Always timing to the Timing algebraic data type\n\nAdds the Always modality (response must hold at every timepoint from the\ncondition onward, i.e. G R) as a seventh Timing variant, completing all\ndispatch sites:\n\n- requirement_to_ltl emits body G R; to_string emits \"always\" (both were\n  previously swallowed by the Eventually catch-all).\n- serialisation to_json/from_json/validator handle the \"Always\" type.\n- syntactic_similarity models Always as the top of the timing order:\n  its downset is every timing except the AfterTicks family.\n- Halstead counting, mutation (placeholder ForTicks weakening), CLI help,\n  and docs updated; fuzzer now differentially tests Always against the\n  real FRET CLI.\n\nVerified: 22/22 tests pass, lint/format clean, and the LTL translation\nmatches FRET's own \"always\" formalisation (checked via the equivalence\nfuzzer with ltlfilt).",
          "timestamp": "2026-07-08T13:45:54+01:00",
          "tree_id": "f3ed5bf6e3c8c27bae692f3639a4b2f600fa2b03",
          "url": "https://github.com/benmandrew/counter/commit/cad434aed8e5b9f81e6688d8378c6a40fd4934b5"
        },
        "date": 1783515581951,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 539.5216483036788,
            "unit": "ns/iter",
            "extra": "iterations: 1307793\ncpu: 539.4695467860739 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2230.203534918055,
            "unit": "ns/iter",
            "extra": "iterations: 308522\ncpu: 2229.9246374650756 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 862.0965634353333,
            "unit": "ns/iter",
            "extra": "iterations: 810897\ncpu: 862.0341547693478 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 161.6528680716383,
            "unit": "ns/iter",
            "extra": "iterations: 4325537\ncpu: 161.6452054392322 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 183.88615583765815,
            "unit": "ns/iter",
            "extra": "iterations: 3777585\ncpu: 183.86197504490312 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 206.86451404970205,
            "unit": "ns/iter",
            "extra": "iterations: 3385089\ncpu: 206.8465216128734 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 251.91482575501166,
            "unit": "ns/iter",
            "extra": "iterations: 2779420\ncpu: 251.89874830000502 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3764.6780692730254,
            "unit": "ns/iter",
            "extra": "iterations: 186624\ncpu: 3764.2696009087776 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "4ae6a4aba57c7f1e7ae3795f2468c4dfc8cdd1ff",
          "message": "feat: add Always timing to the Timing algebraic data type\n\nAdds the Always modality (response must hold at every timepoint from the\ncondition onward, i.e. G R) as a seventh Timing variant, completing all\ndispatch sites:\n\n- requirement_to_ltl emits body G R; to_string emits \"always\" (both were\n  previously swallowed by the Eventually catch-all).\n- serialisation to_json/from_json/validator handle the \"Always\" type.\n- syntactic_similarity models Always as the top of the timing order:\n  its downset is every timing except the AfterTicks family.\n- Halstead counting, mutation (placeholder ForTicks weakening), CLI help,\n  and docs updated; fuzzer now differentially tests Always against the\n  real FRET CLI.\n\nVerified: 22/22 tests pass, lint/format clean, and the LTL translation\nmatches FRET's own \"always\" formalisation (checked via the equivalence\nfuzzer with ltlfilt).",
          "timestamp": "2026-07-08T13:48:07+01:00",
          "tree_id": "d927a0e9b67dec43b5922b3f732e10743c36d476",
          "url": "https://github.com/benmandrew/counter/commit/4ae6a4aba57c7f1e7ae3795f2468c4dfc8cdd1ff"
        },
        "date": 1783515582593,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 544.868124499825,
            "unit": "ns/iter",
            "extra": "iterations: 1294524\ncpu: 544.8090193770066 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2222.90424296673,
            "unit": "ns/iter",
            "extra": "iterations: 319517\ncpu: 2221.8759627813233 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 822.0720566058399,
            "unit": "ns/iter",
            "extra": "iterations: 852774\ncpu: 821.9399325026324 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 166.42440482722225,
            "unit": "ns/iter",
            "extra": "iterations: 4155768\ncpu: 166.40192161833866 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 188.81682908669805,
            "unit": "ns/iter",
            "extra": "iterations: 3698846\ncpu: 188.80265655828865 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 213.37261655422355,
            "unit": "ns/iter",
            "extra": "iterations: 3319721\ncpu: 213.3589105831485 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 256.5203837753991,
            "unit": "ns/iter",
            "extra": "iterations: 2737226\ncpu: 256.4730376666011 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3693.3993226293805,
            "unit": "ns/iter",
            "extra": "iterations: 190147\ncpu: 3693.0164556895456 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "5e336e211b152090bcab5a319273cb1cb30ee351",
          "message": "Merge pull request #6 from benmandrew/feature/internal-doxygen-docs\n\ndocs: add internal implementation reference (private/src Doxygen HTML)",
          "timestamp": "2026-07-08T14:09:52+01:00",
          "tree_id": "061bac905378ed894147db45309507c6335563b0",
          "url": "https://github.com/benmandrew/counter/commit/5e336e211b152090bcab5a319273cb1cb30ee351"
        },
        "date": 1783516782818,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 557.4568251399013,
            "unit": "ns/iter",
            "extra": "iterations: 1309790\ncpu: 557.3882210125288 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2247.4977438810233,
            "unit": "ns/iter",
            "extra": "iterations: 313370\ncpu: 2247.3144557551777 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 877.9373532785822,
            "unit": "ns/iter",
            "extra": "iterations: 789762\ncpu: 877.8215487703891 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 161.66919183397326,
            "unit": "ns/iter",
            "extra": "iterations: 4326314\ncpu: 161.64377435387266 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 184.0978504656349,
            "unit": "ns/iter",
            "extra": "iterations: 3782261\ncpu: 184.0821075013069 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 207.35541379261713,
            "unit": "ns/iter",
            "extra": "iterations: 3391264\ncpu: 207.34526330005565 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 252.16787871091955,
            "unit": "ns/iter",
            "extra": "iterations: 2787852\ncpu: 252.1393348714353 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3817.9349236494763,
            "unit": "ns/iter",
            "extra": "iterations: 182186\ncpu: 3817.0230094518784 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "67a11d09319fe343776e5346284084149558e63e",
          "message": "Merge pull request #7 from benmandrew/feature/non-weakenable-requirements\n\nfeat: non-weakenable (locked) requirements",
          "timestamp": "2026-07-08T14:36:22+01:00",
          "tree_id": "ba1567cdef1a05ef5de87f36e355ae2da65888a2",
          "url": "https://github.com/benmandrew/counter/commit/67a11d09319fe343776e5346284084149558e63e"
        },
        "date": 1783518539708,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 544.9717211135551,
            "unit": "ns/iter",
            "extra": "iterations: 1285871\ncpu: 544.9648572835067 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2222.2000312189984,
            "unit": "ns/iter",
            "extra": "iterations: 313911\ncpu: 2222.0547575586716 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 833.2674095963696,
            "unit": "ns/iter",
            "extra": "iterations: 820266\ncpu: 833.181626448006 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 163.60554974108953,
            "unit": "ns/iter",
            "extra": "iterations: 4305354\ncpu: 163.59001559453642 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 193.09560726999197,
            "unit": "ns/iter",
            "extra": "iterations: 3707239\ncpu: 193.08769491257507 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 205.59454751605082,
            "unit": "ns/iter",
            "extra": "iterations: 3340019\ncpu: 205.58607989954533 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 251.11386139883456,
            "unit": "ns/iter",
            "extra": "iterations: 2731189\ncpu: 251.10734116167 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3906.8959984725275,
            "unit": "ns/iter",
            "extra": "iterations: 183305\ncpu: 3906.6732495021997 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "9dd9688353ef6334a204f0015f1fe63f11a40bf5",
          "message": "Merge pull request #8 from benmandrew/feature/cpu-attribution-report\n\nfeat: add per-process CPU attribution report for CLI tools",
          "timestamp": "2026-07-08T14:41:58+01:00",
          "tree_id": "f083c0a67e9c0582cac0eedf2155a5e9eb2ee6c2",
          "url": "https://github.com/benmandrew/counter/commit/9dd9688353ef6334a204f0015f1fe63f11a40bf5"
        },
        "date": 1783518855358,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 551.4048123036825,
            "unit": "ns/iter",
            "extra": "iterations: 1275065\ncpu: 551.3526808437217 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2287.2677838194195,
            "unit": "ns/iter",
            "extra": "iterations: 303450\ncpu: 2287.0078464326903 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 918.800107544407,
            "unit": "ns/iter",
            "extra": "iterations: 835004\ncpu: 918.7574814012867 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 170.07295882514796,
            "unit": "ns/iter",
            "extra": "iterations: 4111223\ncpu: 170.04436733302967 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 194.79767967985703,
            "unit": "ns/iter",
            "extra": "iterations: 3601141\ncpu: 194.76571481094467 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 217.4056093951964,
            "unit": "ns/iter",
            "extra": "iterations: 3220882\ncpu: 217.3713352429553 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 266.7527599715508,
            "unit": "ns/iter",
            "extra": "iterations: 2624393\ncpu: 266.6961419269139 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3805.9712948932465,
            "unit": "ns/iter",
            "extra": "iterations: 180177\ncpu: 3805.5274091587735 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "aa39bf4c879d4cf03ac829113f0e11677a5e216a",
          "message": "docs: correct stale README setup steps and document counter --config\n\n- .envrc is committed, so drop the \"echo use flake > .envrc\" step; users\n  only need `direnv allow`.\n- Fix broken config-template link (counter.toml.example -> example-config.toml).\n- Add the real `--config <file.toml>` flag to the counter CLI table.",
          "timestamp": "2026-07-08T15:30:45+01:00",
          "tree_id": "442b8f1a60ca1961186eff53cc7b63767b1a876d",
          "url": "https://github.com/benmandrew/counter/commit/aa39bf4c879d4cf03ac829113f0e11677a5e216a"
        },
        "date": 1783524151480,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 541.0211209393923,
            "unit": "ns/iter",
            "extra": "iterations: 1294450\ncpu: 540.994995557959 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2232.9194051730433,
            "unit": "ns/iter",
            "extra": "iterations: 312427\ncpu: 2232.7933085168697 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 838.9207967481412,
            "unit": "ns/iter",
            "extra": "iterations: 831279\ncpu: 838.7992779800765 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 163.74524499532734,
            "unit": "ns/iter",
            "extra": "iterations: 4289796\ncpu: 163.73451581380556 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 186.64093726541486,
            "unit": "ns/iter",
            "extra": "iterations: 3765977\ncpu: 186.61954919002417 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 209.97395508025937,
            "unit": "ns/iter",
            "extra": "iterations: 3347793\ncpu: 209.9517276008402 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 254.57675276753307,
            "unit": "ns/iter",
            "extra": "iterations: 2737100\ncpu: 254.53473201563708 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3889.1462876269666,
            "unit": "ns/iter",
            "extra": "iterations: 179858\ncpu: 3888.830766493566 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "94522258a7e020f770b0247d621f02c9706e7952",
          "message": "feat: compare takes an --ideals directory instead of repeated --ideal\n\nReplace the repeatable --ideal <file> flag with a single --ideals <dir>\nthat loads every .json in the directory as an ideal specification, sorted\nby filename. Update the built-in usage text, README, and CLAUDE.md CLI\nreference to match, and repoint analyse.sh at the new examples layout and\ndirectory-based flag.",
          "timestamp": "2026-07-08T16:53:21+01:00",
          "tree_id": "21415dbbc9fb9dfc0b276eaf9f034ffddb567bfb",
          "url": "https://github.com/benmandrew/counter/commit/94522258a7e020f770b0247d621f02c9706e7952"
        },
        "date": 1783526659349,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 524.0558204433725,
            "unit": "ns/iter",
            "extra": "iterations: 1348789\ncpu: 524.0129130649791 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2047.921167130654,
            "unit": "ns/iter",
            "extra": "iterations: 339465\ncpu: 2047.818900328458 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 802.1189220848656,
            "unit": "ns/iter",
            "extra": "iterations: 865634\ncpu: 802.0851976701468 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 140.42229361207802,
            "unit": "ns/iter",
            "extra": "iterations: 4972384\ncpu: 140.4229198710317 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 160.0113735441577,
            "unit": "ns/iter",
            "extra": "iterations: 4381308\ncpu: 160.00485129098448 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 180.23486735619923,
            "unit": "ns/iter",
            "extra": "iterations: 3855853\ncpu: 180.23223603181967 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 225.39771425590982,
            "unit": "ns/iter",
            "extra": "iterations: 3067710\ncpu: 225.3925948019858 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3714.0782826185978,
            "unit": "ns/iter",
            "extra": "iterations: 192712\ncpu: 3713.754265432354 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "59b9310011ccc2d9ba820a62500fcf612ae18955",
          "message": "Merge pull request #9 from benmandrew/feature/internal-atom-prefix\n\nPrefix atom names internally to avoid LTL operator collisions",
          "timestamp": "2026-07-08T16:56:55+01:00",
          "tree_id": "59c1e2c4833f65c23cb90e9be568426b1eebfd5a",
          "url": "https://github.com/benmandrew/counter/commit/59b9310011ccc2d9ba820a62500fcf612ae18955"
        },
        "date": 1783526997176,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 543.7214697070982,
            "unit": "ns/iter",
            "extra": "iterations: 1307172\ncpu: 543.6000625778398 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2146.941458500227,
            "unit": "ns/iter",
            "extra": "iterations: 304724\ncpu: 2146.594646302884 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 858.2479655128038,
            "unit": "ns/iter",
            "extra": "iterations: 799833\ncpu: 858.0464346932424 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 162.467372680531,
            "unit": "ns/iter",
            "extra": "iterations: 4314835\ncpu: 162.46283044426954 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 192.30931231024255,
            "unit": "ns/iter",
            "extra": "iterations: 3744581\ncpu: 192.2593085848591 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 210.81433473612782,
            "unit": "ns/iter",
            "extra": "iterations: 2960780\ncpu: 210.8074791102344 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 259.84185755211587,
            "unit": "ns/iter",
            "extra": "iterations: 2688829\ncpu: 259.8108131829878 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3694.990210637182,
            "unit": "ns/iter",
            "extra": "iterations: 189900\ncpu: 3694.795876777252 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "f4968abccf2e9ff99a91108d81bf07e9d20fe0b8",
          "message": "feat: keep output atoms out of triggers and never weaken Always\n\nMutation was the source of two degenerate repairs on the fsm example:\nit could inject output atoms into a trigger condition (letting the\nsynthesiser vacuously discharge a guarantee via its own outputs) and it\nweakened Always timing to ForTicks(10).\n\n- mutate_requirement draws trigger atoms from an input-only pool\n  (mutate_specification passes in_atoms, falling back to the full pool\n  only when there are no inputs, since random_atom needs a non-empty\n  pool). Crossover already crosses condition-with-condition only, so the\n  output-free invariant is preserved through evolution.\n- mutate_timing leaves Always unchanged instead of weakening it.\n- Extract creates_duplicate helper (also keeps mutate_specification\n  under the clang-tidy cognitive-complexity threshold).\n- Tests: assert Always is unchanged; add a 200-seed test that trigger\n  mutation never introduces an output atom.",
          "timestamp": "2026-07-08T17:35:01+01:00",
          "tree_id": "acf82b400ffcf83cd61fa1f158189a57e43995a5",
          "url": "https://github.com/benmandrew/counter/commit/f4968abccf2e9ff99a91108d81bf07e9d20fe0b8"
        },
        "date": 1783529421029,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 576.1204516833895,
            "unit": "ns/iter",
            "extra": "iterations: 1198273\ncpu: 576.0381298752454 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2320.694750964847,
            "unit": "ns/iter",
            "extra": "iterations: 301865\ncpu: 2320.5314163616194 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 831.5682402475699,
            "unit": "ns/iter",
            "extra": "iterations: 824616\ncpu: 831.2936481950387 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 161.922500989216,
            "unit": "ns/iter",
            "extra": "iterations: 4339307\ncpu: 161.82271224414416 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 183.56678184016155,
            "unit": "ns/iter",
            "extra": "iterations: 3805563\ncpu: 183.51619615809793 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 206.29176029850973,
            "unit": "ns/iter",
            "extra": "iterations: 3363350\ncpu: 206.27722508808176 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 250.56526577445507,
            "unit": "ns/iter",
            "extra": "iterations: 2791126\ncpu: 250.5565714338944 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3806.8748220832404,
            "unit": "ns/iter",
            "extra": "iterations: 185480\ncpu: 3805.6712421824495 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "1c82833ec7d2ccb5a16a232471c716c910047231",
          "message": "tmp",
          "timestamp": "2026-07-08T18:22:40+01:00",
          "tree_id": "6081fe8fa5eea2c0a0218f8f0a162d96e176a979",
          "url": "https://github.com/benmandrew/counter/commit/1c82833ec7d2ccb5a16a232471c716c910047231"
        },
        "date": 1783532048665,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 571.3084564947133,
            "unit": "ns/iter",
            "extra": "iterations: 1169468\ncpu: 571.2727479503502 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2070.963076620827,
            "unit": "ns/iter",
            "extra": "iterations: 335966\ncpu: 2070.7414113332898 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 872.2531896439068,
            "unit": "ns/iter",
            "extra": "iterations: 809338\ncpu: 872.1408064368652 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 155.94997509996657,
            "unit": "ns/iter",
            "extra": "iterations: 4447785\ncpu: 155.92016947761638 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 178.94929546791346,
            "unit": "ns/iter",
            "extra": "iterations: 3912313\ncpu: 178.8962894839957 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 202.96239274677407,
            "unit": "ns/iter",
            "extra": "iterations: 3475420\ncpu: 202.90429415725288 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 244.72157387567813,
            "unit": "ns/iter",
            "extra": "iterations: 2876034\ncpu: 244.71753845747307 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3601.90954861993,
            "unit": "ns/iter",
            "extra": "iterations: 193850\ncpu: 3601.1724219757534 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "11a2124a8317a40cbee6b2bbcf737a78981316c7",
          "message": "feat: add fsm-combined scenario to run_experiments.py",
          "timestamp": "2026-07-09T13:24:56+01:00",
          "tree_id": "044364290cef584dd82e091f928924710c776645",
          "url": "https://github.com/benmandrew/counter/commit/11a2124a8317a40cbee6b2bbcf737a78981316c7"
        },
        "date": 1783603889684,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 607.7341600453027,
            "unit": "ns/iter",
            "extra": "iterations: 1150824\ncpu: 607.6772304018685 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2412.72937312383,
            "unit": "ns/iter",
            "extra": "iterations: 300809\ncpu: 2412.604459972939 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 846.2713951642029,
            "unit": "ns/iter",
            "extra": "iterations: 832583\ncpu: 846.1856475570604 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 163.35892987971735,
            "unit": "ns/iter",
            "extra": "iterations: 4246289\ncpu: 163.349239300481 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 191.4755277285425,
            "unit": "ns/iter",
            "extra": "iterations: 3734022\ncpu: 191.45645767486096 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 209.42741026195299,
            "unit": "ns/iter",
            "extra": "iterations: 3328549\ncpu: 209.4166202750809 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 255.02852698928024,
            "unit": "ns/iter",
            "extra": "iterations: 2745961\ncpu: 254.9978051399858 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3718.1700908648686,
            "unit": "ns/iter",
            "extra": "iterations: 188852\ncpu: 3717.9218117891246 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "edb4e20421f662a58def17e8962b078f582f5833",
          "message": "feat: add fsm-combined scenario to run_experiments.py",
          "timestamp": "2026-07-09T14:21:31+01:00",
          "tree_id": "044364290cef584dd82e091f928924710c776645",
          "url": "https://github.com/benmandrew/counter/commit/edb4e20421f662a58def17e8962b078f582f5833"
        },
        "date": 1783604053202,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 430.09876287549156,
            "unit": "ns/iter",
            "extra": "iterations: 1632172\ncpu: 430.03611322826276 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1708.0688845230077,
            "unit": "ns/iter",
            "extra": "iterations: 411268\ncpu: 1707.904928173357 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 650.7318503738954,
            "unit": "ns/iter",
            "extra": "iterations: 1080587\ncpu: 650.6602374450181 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 132.3234061804302,
            "unit": "ns/iter",
            "extra": "iterations: 5335971\ncpu: 132.27059236266462 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 149.71637675515058,
            "unit": "ns/iter",
            "extra": "iterations: 4626814\ncpu: 149.6688198401751 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 166.52581729182523,
            "unit": "ns/iter",
            "extra": "iterations: 4141391\ncpu: 166.51221630606707 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 205.67545792302377,
            "unit": "ns/iter",
            "extra": "iterations: 3419079\ncpu: 205.65661278958459 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2922.857245823748,
            "unit": "ns/iter",
            "extra": "iterations: 235860\ncpu: 2922.4602179258895 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "cacd979ed8ac20ac52581b3616b75728d37cced1",
          "message": "feat: write per-run counter logs in run_experiments\n\nRedirect each counter invocation's stdout+stderr to run.log in its\noutput dir instead of discarding stdout and piping stderr. Failed or\nzero-repair runs previously left no trace explaining why; now every run\nis self-documenting. On non-zero exit the console shows the last log\nline via a new tail_line() helper (stripping carriage-return progress\nspam) and points at the full log.",
          "timestamp": "2026-07-09T14:28:25+01:00",
          "tree_id": "7077377a28beba3293105db39eb5b34083e22672",
          "url": "https://github.com/benmandrew/counter/commit/cacd979ed8ac20ac52581b3616b75728d37cced1"
        },
        "date": 1783604263789,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 551.8866421558932,
            "unit": "ns/iter",
            "extra": "iterations: 1283246\ncpu: 551.8629039170977 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2262.1882728295686,
            "unit": "ns/iter",
            "extra": "iterations: 309981\ncpu: 2261.9533132675874 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 875.8329670794733,
            "unit": "ns/iter",
            "extra": "iterations: 803632\ncpu: 875.8289515599176 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 163.6911305611386,
            "unit": "ns/iter",
            "extra": "iterations: 4280485\ncpu: 163.67855838765934 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 186.17211179174848,
            "unit": "ns/iter",
            "extra": "iterations: 3753694\ncpu: 186.16347603187694 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 209.04725477503635,
            "unit": "ns/iter",
            "extra": "iterations: 3332277\ncpu: 209.02474854281337 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 254.4303903380854,
            "unit": "ns/iter",
            "extra": "iterations: 2751256\ncpu: 254.4178044500402 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3815.6185712418683,
            "unit": "ns/iter",
            "extra": "iterations: 183628\ncpu: 3815.238149955342 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "d7cb33f871489a27874d43151eb718c9cc115582",
          "message": "feat: skip gen40 x {fsm-timing,fsm-combined} in run_experiments sweep\n\nThese cells time out (3600s cap) on almost every seed — per-generation\ncost grows superlinearly with generation count for these two\nstructurally complex specs, likely from the bloat filter's fixed 2.0\nmax_ratio letting formula complexity ratchet up each generation.\nExcluding them reclaims ~20 CPU-hours of pure timeouts per full sweep\nrun.",
          "timestamp": "2026-07-10T14:09:04+01:00",
          "tree_id": "e8c7f7e37f3656517ccbf88f152d4cafaad865a8",
          "url": "https://github.com/benmandrew/counter/commit/d7cb33f871489a27874d43151eb718c9cc115582"
        },
        "date": 1783689493731,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 542.4318245862895,
            "unit": "ns/iter",
            "extra": "iterations: 1291712\ncpu: 542.3844850864589 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2254.319629587253,
            "unit": "ns/iter",
            "extra": "iterations: 306145\ncpu: 2254.139574384687 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 852.1474821031819,
            "unit": "ns/iter",
            "extra": "iterations: 810200\ncpu: 852.1337509256975 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 163.02677788497448,
            "unit": "ns/iter",
            "extra": "iterations: 4278792\ncpu: 163.0204569420528 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 186.0858887968136,
            "unit": "ns/iter",
            "extra": "iterations: 3766277\ncpu: 186.07941662283474 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 209.01329533299963,
            "unit": "ns/iter",
            "extra": "iterations: 3345234\ncpu: 209.00163845040433 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 255.73531556468092,
            "unit": "ns/iter",
            "extra": "iterations: 2755077\ncpu: 255.72958577927227 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3752.936519242895,
            "unit": "ns/iter",
            "extra": "iterations: 186718\ncpu: 3752.727337482191 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "3f4992034beef088dc9dc817deb6fb6709453300",
          "message": "feat: add --base flag to recompare for per-machine result dirs\n\nrecompare now targets any directory holding results.csv + results/\n(default: repo experiments/), so results rsynced from multiple machines\ninto separate dirs (results-av2, results-av3) can each be recompared\nbefore combining.",
          "timestamp": "2026-07-10T17:03:10+01:00",
          "tree_id": "abb49f86a8fbd031d597784fce9eec8a911cfeeb",
          "url": "https://github.com/benmandrew/counter/commit/3f4992034beef088dc9dc817deb6fb6709453300"
        },
        "date": 1783700449905,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 432.3763930761519,
            "unit": "ns/iter",
            "extra": "iterations: 1624642\ncpu: 432.3339948123956 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1708.9984581332342,
            "unit": "ns/iter",
            "extra": "iterations: 412487\ncpu: 1708.7170650226549 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 665.1814896232505,
            "unit": "ns/iter",
            "extra": "iterations: 1033993\ncpu: 665.0877588146152 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 132.83787701119303,
            "unit": "ns/iter",
            "extra": "iterations: 5274545\ncpu: 132.82192359720128 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 153.9986520344802,
            "unit": "ns/iter",
            "extra": "iterations: 4647745\ncpu: 153.98293495017475 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 166.46894339323694,
            "unit": "ns/iter",
            "extra": "iterations: 4050523\ncpu: 166.43081992127918 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 206.11926571018634,
            "unit": "ns/iter",
            "extra": "iterations: 3405195\ncpu: 206.10173778594168 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2914.5416514401277,
            "unit": "ns/iter",
            "extra": "iterations: 240808\ncpu: 2914.138305205808 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "a9fa0f67cb239de9bb9fdba687cc833e03da47cf",
          "message": "Merge pull request #10 from benmandrew/feat/tlsf-mode\n\nAdd TLSF / GR(1) mode via genetic-algorithm abstraction",
          "timestamp": "2026-07-10T17:31:27+01:00",
          "tree_id": "bb820ca6dea74502ae4d6a27103f330f6dddef45",
          "url": "https://github.com/benmandrew/counter/commit/a9fa0f67cb239de9bb9fdba687cc833e03da47cf"
        },
        "date": 1783702082017,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 548.8162523020483,
            "unit": "ns/iter",
            "extra": "iterations: 1279831\ncpu: 548.658487722207 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2275.0588195485757,
            "unit": "ns/iter",
            "extra": "iterations: 310305\ncpu: 2274.9510932791936 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 853.2676962159114,
            "unit": "ns/iter",
            "extra": "iterations: 820359\ncpu: 853.2335965107955 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 167.22489465446992,
            "unit": "ns/iter",
            "extra": "iterations: 4297999\ncpu: 167.18662219325782 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 189.88881650517087,
            "unit": "ns/iter",
            "extra": "iterations: 3817617\ncpu: 189.80754853092895 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 214.3502009194405,
            "unit": "ns/iter",
            "extra": "iterations: 3185854\ncpu: 214.2451609521341 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 259.31630383660405,
            "unit": "ns/iter",
            "extra": "iterations: 2789552\ncpu: 259.1956371489042 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3384.2313054041497,
            "unit": "ns/iter",
            "extra": "iterations: 207453\ncpu: 3383.723976033127 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "82e64ac0482a18111b0f55facf94a495af8661bc",
          "message": "feat: temporal-structure mutation for TLSF specifications\n\nTLSF mutation previously rewrote only propositional subtrees inside\nfixed temporal boundaries, so a formula's X/F/G/U/R/W skeleton could\nnever change. Add a recursive re-implementation of the mutation operator\nfrom Brizzio et al., \"Automated Repair of Unrealisable LTL\nSpecifications Guided by Model Counting\", which may insert, drop, or swap\ntemporal operators.\n\nOnce a section formula is chosen, cfg.tlsf_p_temporal (default 0.2)\nselects the temporal-structure rewrite over the skeleton-preserving one.\nAtom pools stay side-correct: assumption-side rewrites draw inputs only.",
          "timestamp": "2026-07-10T17:48:57+01:00",
          "tree_id": "3897f9ec46782825478e42cdafbc5d2a53ffd8c7",
          "url": "https://github.com/benmandrew/counter/commit/82e64ac0482a18111b0f55facf94a495af8661bc"
        },
        "date": 1783703012130,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 428.77791481544386,
            "unit": "ns/iter",
            "extra": "iterations: 1580263\ncpu: 428.7415056860788 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1714.4815059509776,
            "unit": "ns/iter",
            "extra": "iterations: 408672\ncpu: 1714.3368496006578 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 655.4297778938852,
            "unit": "ns/iter",
            "extra": "iterations: 1064266\ncpu: 655.3474403955402 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 130.37451700466838,
            "unit": "ns/iter",
            "extra": "iterations: 5345031\ncpu: 130.3518168556927 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 149.59133852029376,
            "unit": "ns/iter",
            "extra": "iterations: 4733025\ncpu: 149.54202270218303 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 165.08450331772454,
            "unit": "ns/iter",
            "extra": "iterations: 4220213\ncpu: 165.03545911071302 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 205.07653047426027,
            "unit": "ns/iter",
            "extra": "iterations: 3512653\ncpu: 205.02105445656045 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2668.9744429056873,
            "unit": "ns/iter",
            "extra": "iterations: 262119\ncpu: 2668.6625120651324 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "e62afaf98efbbf003ddb68c66fc3f21be0327df2",
          "message": "docs: document the TLSF workflow in the README\n\nThe README described only the FRETISH flow. Cover TLSF as a first-class\ninput: --format/extension selection, the six evolved sections, the\nrepair_N.tlsf + repair_N.fitness.json outputs, and a runnable\nexamples/arbiter-gr1 walkthrough.",
          "timestamp": "2026-07-10T17:54:00+01:00",
          "tree_id": "761f93171815cccb08be85ee128c937a4d56e229",
          "url": "https://github.com/benmandrew/counter/commit/e62afaf98efbbf003ddb68c66fc3f21be0327df2"
        },
        "date": 1783703063974,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 551.5411354974854,
            "unit": "ns/iter",
            "extra": "iterations: 1279175\ncpu: 551.5072581937578 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2232.1409082876007,
            "unit": "ns/iter",
            "extra": "iterations: 299883\ncpu: 2231.9256276614547 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 873.0757053504177,
            "unit": "ns/iter",
            "extra": "iterations: 810590\ncpu: 872.9608359343194 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 160.87075326285031,
            "unit": "ns/iter",
            "extra": "iterations: 4324055\ncpu: 160.84899498271878 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 184.78169094438775,
            "unit": "ns/iter",
            "extra": "iterations: 3812343\ncpu: 184.7605601069998 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 205.8352944920178,
            "unit": "ns/iter",
            "extra": "iterations: 3393912\ncpu: 205.82440911844495 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 250.27498007458135,
            "unit": "ns/iter",
            "extra": "iterations: 2796679\ncpu: 250.25268005373496 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3382.211756175359,
            "unit": "ns/iter",
            "extra": "iterations: 206870\ncpu: 3382.02083917436 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "29500f845479e8cd6e6424f9b4a7d58776a44468",
          "message": "feat: elitist selection via configurable elitism_rate\n\nThe genetic algorithm was not elitist: every candidate carried into the\nnext generation could still be mutated, crossed over, or dropped by an\noffspring filter, so the fittest specification of a generation could be\nlost to a stochastic operator.\n\nAdd a genetic.elitism_rate (default 0.1). The top elitism_rate fraction\nof each generation now carries over verbatim, bypassing crossover,\nmutation, and the offspring filters; the remaining slots are bred as\nbefore. The rate must be strictly less than selection_rate, since the\nelites are a subset of the selected parents, and this is enforced when\nthe config is loaded.\n\nElitism, like selection_rate, applies to FRETISH mode only. TLSF mode\nkeeps the full population each generation (no truncation selection), so\nit passes an elitism size of zero.",
          "timestamp": "2026-07-13T13:32:01+01:00",
          "tree_id": "4b9c97578eb2b05d35b152cad0f332206e64b42e",
          "url": "https://github.com/benmandrew/counter/commit/29500f845479e8cd6e6424f9b4a7d58776a44468"
        },
        "date": 1783946780975,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 418.46331810951017,
            "unit": "ns/iter",
            "extra": "iterations: 1655081\ncpu: 418.4242873913724 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1654.4916658598984,
            "unit": "ns/iter",
            "extra": "iterations: 423499\ncpu: 1654.2502579699124 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 657.0039483918302,
            "unit": "ns/iter",
            "extra": "iterations: 1060685\ncpu: 656.8875688823731 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 126.00849786804515,
            "unit": "ns/iter",
            "extra": "iterations: 5542802\ncpu: 125.99710543512109 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 143.13693077095488,
            "unit": "ns/iter",
            "extra": "iterations: 4895503\ncpu: 143.12816149842007 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 161.6210254426113,
            "unit": "ns/iter",
            "extra": "iterations: 4370581\ncpu: 161.60344311202562 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 195.292823419132,
            "unit": "ns/iter",
            "extra": "iterations: 3606829\ncpu: 195.27590717497282 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2600.770373935615,
            "unit": "ns/iter",
            "extra": "iterations: 270100\ncpu: 2600.422380599777 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "2a519fdf7fb3b22078e32da249f7a182b20f9b71",
          "message": "Merge pull request #15 from benmandrew/worktree-nsga2-selection\n\nfeat: NSGA-II multi-objective selection",
          "timestamp": "2026-07-13T15:17:41+01:00",
          "tree_id": "c54a00f9309647e96dabd4c142b77c4053ec782f",
          "url": "https://github.com/benmandrew/counter/commit/2a519fdf7fb3b22078e32da249f7a182b20f9b71"
        },
        "date": 1783953192438,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 531.6422743550335,
            "unit": "ns/iter",
            "extra": "iterations: 1325035\ncpu: 531.5929299980755 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2210.7457056247736,
            "unit": "ns/iter",
            "extra": "iterations: 317683\ncpu: 2210.576354416195 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 857.2413949712213,
            "unit": "ns/iter",
            "extra": "iterations: 808074\ncpu: 857.1929340629695 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 160.4197192581394,
            "unit": "ns/iter",
            "extra": "iterations: 4363724\ncpu: 160.40018021304735 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 189.78965218417287,
            "unit": "ns/iter",
            "extra": "iterations: 3806871\ncpu: 189.75003986213352 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 205.16819434133902,
            "unit": "ns/iter",
            "extra": "iterations: 3408230\ncpu: 205.13215158601383 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 250.1251111550765,
            "unit": "ns/iter",
            "extra": "iterations: 2800142\ncpu: 250.07860029955606 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3409.9140139006686,
            "unit": "ns/iter",
            "extra": "iterations: 208336\ncpu: 3409.370680055294 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "31d078729cdde835da2c6e3891caa45bfa42c18b",
          "message": "fix: bounded requirement pairing in similarity scoring for uneven specs\n\nThe syntactic and semantic specification-similarity functions paired\nrequirements by index assuming both specifications had the same number of\nassumptions and guarantees, guarding the invariant with an assert. The\np_add_assumption mutation breaks it: a candidate can carry more assumptions\nthan the original it is scored against. average_timing_similarity looped over\nthe first spec's counts while indexing the second, and the semantic overload\nadvanced a second iterator in lockstep to the first spec's end — both read past\nthe shorter spec once the candidate was longer. In debug builds the assert\nfired; under NDEBUG (release/relwithdebinfo) the assert is a no-op, so the\nout-of-bounds read silently corrupted the score or crashed mid-generation.\n\nPair requirements only over the count the two specs share and skip the\nunmatched surplus (timing similarity additionally normalises by the larger\nstructure so a size difference lowers the score); behaviour is unchanged when\nthe counts match. Regression tests score a candidate with an extra assumption\nagainst a shorter original in both functions and both argument orders.\n\nSurfaced while testing NSGA-II selection but pre-existing and independent of\nit; reproduces on unmodified main.",
          "timestamp": "2026-07-13T15:23:59+01:00",
          "tree_id": "b8e44fc3927544bdbd2a5e24081442efb458a805",
          "url": "https://github.com/benmandrew/counter/commit/31d078729cdde835da2c6e3891caa45bfa42c18b"
        },
        "date": 1783953297645,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 541.7869369277607,
            "unit": "ns/iter",
            "extra": "iterations: 1324941\ncpu: 541.6730873299264 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2209.075862941772,
            "unit": "ns/iter",
            "extra": "iterations: 308161\ncpu: 2208.7422970460243 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 876.2518048620562,
            "unit": "ns/iter",
            "extra": "iterations: 817514\ncpu: 876.1717719818864 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 164.30880466555155,
            "unit": "ns/iter",
            "extra": "iterations: 4362528\ncpu: 164.3018506700702 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 182.76007745874495,
            "unit": "ns/iter",
            "extra": "iterations: 3822938\ncpu: 182.73836457719173 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 205.17577366915657,
            "unit": "ns/iter",
            "extra": "iterations: 3411148\ncpu: 205.1721505487303 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 249.95844079005923,
            "unit": "ns/iter",
            "extra": "iterations: 2802291\ncpu: 249.9396536619503 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3431.360911377031,
            "unit": "ns/iter",
            "extra": "iterations: 203604\ncpu: 3431.134756684542 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "e79113a3a6eabee7d2634baff89c686a947e38ee",
          "message": "Merge pull request #16 from benmandrew/ci/parallelize-and-speedup\n\nperf(ci): parallelise jobs, scope PR linting, switch to Ninja",
          "timestamp": "2026-07-13T16:30:34+01:00",
          "tree_id": "193bff58d67bccb19f3c933b4bc9414a2db25d62",
          "url": "https://github.com/benmandrew/counter/commit/e79113a3a6eabee7d2634baff89c686a947e38ee"
        },
        "date": 1783957906722,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 527.7056855113991,
            "unit": "ns/iter",
            "extra": "iterations: 1308607\ncpu: 527.6914780373328 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2266.714127457532,
            "unit": "ns/iter",
            "extra": "iterations: 313234\ncpu: 2266.642950637543 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 866.5791152407745,
            "unit": "ns/iter",
            "extra": "iterations: 812334\ncpu: 866.5664517304458 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 161.0526816985281,
            "unit": "ns/iter",
            "extra": "iterations: 4362198\ncpu: 161.03964973621098 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 182.70894271853095,
            "unit": "ns/iter",
            "extra": "iterations: 3835970\ncpu: 182.7021738960419 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 205.45518526876646,
            "unit": "ns/iter",
            "extra": "iterations: 3412070\ncpu: 205.43367310752714 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 250.0155754145841,
            "unit": "ns/iter",
            "extra": "iterations: 2791900\ncpu: 250.00872237544328 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3343.985742076422,
            "unit": "ns/iter",
            "extra": "iterations: 209217\ncpu: 3343.730920527493 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "d488b5c17cffbd68861aca4e036b22971825dd65",
          "message": "Merge pull request #17 from benmandrew/feature/quick-experiment-runner\n\nfeat: profile-based quick experiment runner with parallel jobs",
          "timestamp": "2026-07-13T17:35:25+01:00",
          "tree_id": "4916ab48348e132358fac7b2d39d15047fc9e8f5",
          "url": "https://github.com/benmandrew/counter/commit/d488b5c17cffbd68861aca4e036b22971825dd65"
        },
        "date": 1783960759596,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 459.8769051228685,
            "unit": "ns/iter",
            "extra": "iterations: 1532972\ncpu: 459.87844396375147 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1825.309104048879,
            "unit": "ns/iter",
            "extra": "iterations: 381907\ncpu: 1824.8981479784343 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 736.5617319959601,
            "unit": "ns/iter",
            "extra": "iterations: 949702\ncpu: 736.4185628755127 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 128.9203970897224,
            "unit": "ns/iter",
            "extra": "iterations: 5384778\ncpu: 128.91909471476822 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 145.30213212392763,
            "unit": "ns/iter",
            "extra": "iterations: 5006088\ncpu: 145.29910520949684 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 161.49758809572873,
            "unit": "ns/iter",
            "extra": "iterations: 4365223\ncpu: 161.49152975689898 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 203.10538405206535,
            "unit": "ns/iter",
            "extra": "iterations: 3438414\ncpu: 203.09242342545124 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3037.528945792888,
            "unit": "ns/iter",
            "extra": "iterations: 238843\ncpu: 3037.410411860509 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "828b786279c55764c375a278ff242412a0c611d8",
          "message": "Merge pull request #18 from benmandrew/simplify-boolean-constants\n\nFold boolean constants and contradictions in the propositional simplifier",
          "timestamp": "2026-07-13T17:38:15+01:00",
          "tree_id": "6d003291fe9a6f90c7135a7f465c08921227e99f",
          "url": "https://github.com/benmandrew/counter/commit/828b786279c55764c375a278ff242412a0c611d8"
        },
        "date": 1783960930848,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 530.382724075212,
            "unit": "ns/iter",
            "extra": "iterations: 1330580\ncpu: 530.3188977739032 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2229.585096616602,
            "unit": "ns/iter",
            "extra": "iterations: 313714\ncpu: 2229.503149365345 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 886.5880469717923,
            "unit": "ns/iter",
            "extra": "iterations: 794008\ncpu: 886.4757080533196 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 160.5500362573245,
            "unit": "ns/iter",
            "extra": "iterations: 4346708\ncpu: 160.54423784620454 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 184.32486713133008,
            "unit": "ns/iter",
            "extra": "iterations: 3786822\ncpu: 184.2979876001564 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 205.710698856141,
            "unit": "ns/iter",
            "extra": "iterations: 3389596\ncpu: 205.7025123348032 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 250.49854710710204,
            "unit": "ns/iter",
            "extra": "iterations: 2789951\ncpu: 250.47811054746114 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3358.5245312627126,
            "unit": "ns/iter",
            "extra": "iterations: 209019\ncpu: 3358.439046211108 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "42b212918150dffffc8391f66ed2cda0f1d29e58",
          "message": "chore: amend .gitignore",
          "timestamp": "2026-07-13T17:42:02+01:00",
          "tree_id": "ffa5a6d725cb520dc7d79f4eb3f17618bfeaa1b2",
          "url": "https://github.com/benmandrew/counter/commit/42b212918150dffffc8391f66ed2cda0f1d29e58"
        },
        "date": 1783961135332,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 530.9082911001988,
            "unit": "ns/iter",
            "extra": "iterations: 1322816\ncpu: 530.8217295527118 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2253.989149213582,
            "unit": "ns/iter",
            "extra": "iterations: 311406\ncpu: 2253.8904773832232 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 866.900465216243,
            "unit": "ns/iter",
            "extra": "iterations: 791030\ncpu: 866.8494974906135 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 160.32213025008593,
            "unit": "ns/iter",
            "extra": "iterations: 4370285\ncpu: 160.31254391876044 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 182.76402149478682,
            "unit": "ns/iter",
            "extra": "iterations: 3839070\ncpu: 182.74701320892802 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 205.20279214865027,
            "unit": "ns/iter",
            "extra": "iterations: 3420305\ncpu: 205.19716399560875 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 250.13837750448153,
            "unit": "ns/iter",
            "extra": "iterations: 2801980\ncpu: 250.11381558754906 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3351.056967672641,
            "unit": "ns/iter",
            "extra": "iterations: 209575\ncpu: 3350.9554383872105 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "869b0a7c4f89fc23fe085bf970c2504a82847ece",
          "message": "docs: silence Breathe warnings in docs build\n\nTwo structural warnings surfaced on every docs build:\n\n- The genetic crossover/mutation/operators pages invoked `doxygenfile`\n  with bare basenames that also exist under `include/tlsf/`, so Breathe\n  reported \"multiple matches\". Qualify them with `genetic/`, matching how\n  the tlsf pages already disambiguate.\n\n- Every `include/tlsf/*.hpp` opens `namespace tlsf`, and Breathe re-emits\n  that namespace declaration on each per-file page; Sphinx's C++ domain\n  flags the repeats as cross-page duplicate declarations. Suppress the\n  `duplicate_declaration.cpp` subtype, which is an inherent artifact of\n  rendering one namespace across many `doxygenfile` pages.",
          "timestamp": "2026-07-13T17:47:57+01:00",
          "tree_id": "10ee82244987f689368f8854525bc8d8c9022c35",
          "url": "https://github.com/benmandrew/counter/commit/869b0a7c4f89fc23fe085bf970c2504a82847ece"
        },
        "date": 1783962066852,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 653.2322384542422,
            "unit": "ns/iter",
            "extra": "iterations: 1054089\ncpu: 653.1819808384303 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2471.271633145609,
            "unit": "ns/iter",
            "extra": "iterations: 279675\ncpu: 2470.849886475373 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 864.2618473309966,
            "unit": "ns/iter",
            "extra": "iterations: 809254\ncpu: 864.1751650285323 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 196.42987803675737,
            "unit": "ns/iter",
            "extra": "iterations: 3551070\ncpu: 196.4131247764758 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 222.4304730024535,
            "unit": "ns/iter",
            "extra": "iterations: 3139730\ncpu: 222.41781681864353 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 246.39966246268833,
            "unit": "ns/iter",
            "extra": "iterations: 2844130\ncpu: 246.38012889706167 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 299.2482883933034,
            "unit": "ns/iter",
            "extra": "iterations: 2333480\ncpu: 299.23960608190345 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3317.482083838596,
            "unit": "ns/iter",
            "extra": "iterations: 210285\ncpu: 3317.2958223363544 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "d9d361228084f0565bde1af8f983c284fa325bac",
          "message": "fix: make the fsm-family example fixes realisable\n\nEvery hand-written ideal fix under examples/fsm*/fixes/ was itself\nunrealisable, so implies_ideal could never reach 1 on the fsm family: a\nrealisable repair that implies an unrealisable ideal would witness the\nideal's realisability, a contradiction. The 0% implies-ideal rates in\nboth parameter sweeps were a benchmark defect, not a search failure.\n\nTwo defects, two changes. A *trigger* condition with Always timing\nlatches G(response) forever, so making two conditions disjoint at a\nsingle timepoint still lets the environment raise one trigger and later\nthe other, forcing contradictory outputs for good. I moved each\ncondition into the response as an implication under a trivial trigger,\nso the obligation is checked pointwise. Separately, fsm-combined stated\n!(state_nominal & state_maneuver) as a guarantee over two input atoms\nthe controller cannot influence; the environment violates it at will,\nwhich doomed every variant. That invariant is now an assumption — the\nonly encoding that gives the plant's state-enum discipline any force in\na realisability setting. The output-atom mutual exclusion stays a\nguarantee, since the controller owns those atoms.\n\nVerified with build-release/realize: all 3 input specs remain\nUNREALIZABLE and all 14 fixes now report REALIZABLE.",
          "timestamp": "2026-07-14T15:18:10+01:00",
          "tree_id": "1f6c3e33a0dee921c219a882477bb984f230bb82",
          "url": "https://github.com/benmandrew/counter/commit/d9d361228084f0565bde1af8f983c284fa325bac"
        },
        "date": 1784039282889,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 487.3054633142772,
            "unit": "ns/iter",
            "extra": "iterations: 1431933\ncpu: 487.250888833486 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2041.0737775936802,
            "unit": "ns/iter",
            "extra": "iterations: 342787\ncpu: 2040.7278776616379 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 792.4107706977513,
            "unit": "ns/iter",
            "extra": "iterations: 877789\ncpu: 792.2710036238776 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 153.75522994844553,
            "unit": "ns/iter",
            "extra": "iterations: 4595791\ncpu: 153.68229168819903 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 173.36691694025112,
            "unit": "ns/iter",
            "extra": "iterations: 3953407\ncpu: 173.3287554253837 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 197.97906901488426,
            "unit": "ns/iter",
            "extra": "iterations: 3563043\ncpu: 197.93363453654658 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 234.8266003674766,
            "unit": "ns/iter",
            "extra": "iterations: 2977613\ncpu: 234.78979639059867 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3145.58126125962,
            "unit": "ns/iter",
            "extra": "iterations: 221477\ncpu: 3145.1430125927277 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "18e56cce2356719b18ba83318530c7aa76ef6493",
          "message": "Merge pull request #19 from benmandrew/fix/black-boolean-constants\n\nStop discarding ltlfilt's constant folding; fix black's boolean constants",
          "timestamp": "2026-07-14T16:42:29+01:00",
          "tree_id": "03546ef3e83948a3eed0620e5c1efc39205a254a",
          "url": "https://github.com/benmandrew/counter/commit/18e56cce2356719b18ba83318530c7aa76ef6493"
        },
        "date": 1784044032569,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 547.7566996743857,
            "unit": "ns/iter",
            "extra": "iterations: 1279801\ncpu: 547.6851619900282 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2278.0035903414478,
            "unit": "ns/iter",
            "extra": "iterations: 311391\ncpu: 2277.260370402484 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 612.3221995837167,
            "unit": "ns/iter",
            "extra": "iterations: 1136215\ncpu: 612.2043565698395 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 161.60973546423762,
            "unit": "ns/iter",
            "extra": "iterations: 4334688\ncpu: 161.58683623827136 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 183.35898504171388,
            "unit": "ns/iter",
            "extra": "iterations: 3749159\ncpu: 183.3360273597358 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 206.16278831751805,
            "unit": "ns/iter",
            "extra": "iterations: 3341585\ncpu: 206.14986421114546 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 255.11101002472967,
            "unit": "ns/iter",
            "extra": "iterations: 2794090\ncpu: 255.07792555000015 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3366.051530671463,
            "unit": "ns/iter",
            "extra": "iterations: 206576\ncpu: 3365.797415963134 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "76de38d844662db1f9715372ad1251f9a8e4b993",
          "message": "Merge pull request #20 from benmandrew/worktree-readme-restructure\n\ndocs: restructure the README around a first read",
          "timestamp": "2026-07-14T16:49:12+01:00",
          "tree_id": "63d54a5018edd38cacc3a07c73bd279c885b46ec",
          "url": "https://github.com/benmandrew/counter/commit/76de38d844662db1f9715372ad1251f9a8e4b993"
        },
        "date": 1784044368690,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 274.51284633253516,
            "unit": "ns/iter",
            "extra": "iterations: 2540297\ncpu: 274.482988012819 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1176.2912788216345,
            "unit": "ns/iter",
            "extra": "iterations: 599896\ncpu: 1176.2349657273928 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 292.86974384795184,
            "unit": "ns/iter",
            "extra": "iterations: 2474624\ncpu: 292.83340984327305 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 103.49741967983392,
            "unit": "ns/iter",
            "extra": "iterations: 6807101\ncpu: 103.49019193339426 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 118.16111816651268,
            "unit": "ns/iter",
            "extra": "iterations: 5958397\ncpu: 118.14991431420229 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 132.02667345725735,
            "unit": "ns/iter",
            "extra": "iterations: 5332867\ncpu: 132.01782643369887 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 155.01171262323177,
            "unit": "ns/iter",
            "extra": "iterations: 4372462\ncpu: 154.9973634990997 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 1784.2154233939405,
            "unit": "ns/iter",
            "extra": "iterations: 394349\ncpu: 1784.134487471757 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "b03974e9e497c8a9c3ad84e397b1ec8a571740d3",
          "message": "Merge pull request #21 from benmandrew/fix/merge-experiments-profile\n\nFix the experiment scripts' silent failure modes",
          "timestamp": "2026-07-14T17:48:43+01:00",
          "tree_id": "409171ca6265ee7badf227fdd27aacdba9e382f0",
          "url": "https://github.com/benmandrew/counter/commit/b03974e9e497c8a9c3ad84e397b1ec8a571740d3"
        },
        "date": 1784047950063,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 552.0417008702667,
            "unit": "ns/iter",
            "extra": "iterations: 1283978\ncpu: 551.9896002891016 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2232.4440668173434,
            "unit": "ns/iter",
            "extra": "iterations: 308064\ncpu: 2232.3147819933524 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 608.7083163266221,
            "unit": "ns/iter",
            "extra": "iterations: 1063306\ncpu: 608.6137565291644 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 161.10051456287027,
            "unit": "ns/iter",
            "extra": "iterations: 4347768\ncpu: 161.07540167736644 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 183.84015397517945,
            "unit": "ns/iter",
            "extra": "iterations: 3813082\ncpu: 183.81488858618843 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 206.08297788547713,
            "unit": "ns/iter",
            "extra": "iterations: 3389855\ncpu: 206.06700729087245 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 250.99768757740037,
            "unit": "ns/iter",
            "extra": "iterations: 2789715\ncpu: 250.97685570031348 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3391.294062483441,
            "unit": "ns/iter",
            "extra": "iterations: 206871\ncpu: 3391.072682976348 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "b40ae6631ce847ecc530d658b6dcaa21668c8f58",
          "message": "ci: stop runner noise failing the benchmark job\n\nThe benchmark job failed on every PR that tripped the 150% threshold, and not\nfor the reason it appeared to. github-action-benchmark posts its alert as a\npull request review, but the job granted only contents: write, so the POST to\n/pulls/N/reviews came back 403 and the action threw. A docs-only commit on\nPR #21 failed identically, which is what gave it away.\n\nThe alert itself was noise. That branch changed no compiled code — four files,\nall Python, Markdown and notebook JSON — yet every benchmark came out 1.5-2x\nslower than the baseline: 543.5ns against 274.5ns on small-formula syntactic\nsimilarity, 603.9ns against 292.9ns on the implication check, 3388.3ns against\n1784.2ns on mutation. A uniform ~1.9x across eight unrelated benchmarks, from\na byte-identical binary, is what comparing absolute ns/iter across runner\ninstances looks like. It is not a regression.\n\nGranting pull-requests: write lets the comment post, so the 403 goes away.\nfail-on-alert is now false, so host contention no longer fails unrelated\nbuilds. The alert still surfaces as a comment, which is where that signal\nbelongs.",
          "timestamp": "2026-07-14T17:53:17+01:00",
          "tree_id": "18bfed86ff44606b6d0ee7477ba4aec349dc8a54",
          "url": "https://github.com/benmandrew/counter/commit/b40ae6631ce847ecc530d658b6dcaa21668c8f58"
        },
        "date": 1784048198324,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 554.744312291055,
            "unit": "ns/iter",
            "extra": "iterations: 1254855\ncpu: 554.7080945607262 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2196.083968963494,
            "unit": "ns/iter",
            "extra": "iterations: 319237\ncpu: 2195.788079076047 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 561.0428202574938,
            "unit": "ns/iter",
            "extra": "iterations: 1238199\ncpu: 561.0372791449516 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 167.86195660234242,
            "unit": "ns/iter",
            "extra": "iterations: 4193913\ncpu: 167.84514080287306 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 191.4557401624594,
            "unit": "ns/iter",
            "extra": "iterations: 3710226\ncpu: 191.45212421022316 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 212.49684770127587,
            "unit": "ns/iter",
            "extra": "iterations: 3315517\ncpu: 212.47410976930595 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 257.99417205388903,
            "unit": "ns/iter",
            "extra": "iterations: 2718625\ncpu: 257.97890771989506 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3266.5594720648724,
            "unit": "ns/iter",
            "extra": "iterations: 214193\ncpu: 3265.2371786192844 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "c0a64a94b59c72fe1a03f7998c5718bea221db95",
          "message": "refactor: drop the quick and smoke experiment profiles\n\nThe reduced profiles traded coverage for wall-clock back when the full sweep\ncost 70+ hours. It now costs about 29 minutes at --jobs 4 on 32 cores,\nmeasured across two machines splitting seeds 0-29: 690 runs each, in 13.2 and\n13.9 minutes. Against that, quick finished in roughly 2.5 minutes and smoke in\nabout 1.\n\nThat saving was not free. Both profiles dropped fsm-combined on the grounds\nthat it had never produced a repair, and it now finds one in 328 of 418 runs,\nso the exclusion was discarding real data rather than dead weight. quick was\nalso the default, and it held sweep C back unless asked, so an out-of-the-box\nrun measured two sweeps of the three.\n\nfull is now the only profile and the default. The PROFILES machinery stays as\nit is — sweeps, levels, specs, seeds, timeout_caps and a per-profile CSV are\nstill how a profile is defined — because a wider one is next. Nothing writes\nresults-quick.csv or results-smoke.csv any more, and the copies on av2 and av3\nare deleted.",
          "timestamp": "2026-07-14T19:03:07+01:00",
          "tree_id": "6772a7d914b851eb5a538e5d7b096d4036f40640",
          "url": "https://github.com/benmandrew/counter/commit/c0a64a94b59c72fe1a03f7998c5718bea221db95"
        },
        "date": 1784052695503,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 547.5642388511807,
            "unit": "ns/iter",
            "extra": "iterations: 1283678\ncpu: 547.5493036415675 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2234.7706659351547,
            "unit": "ns/iter",
            "extra": "iterations: 313499\ncpu: 2234.4723141062655 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 610.4991564796921,
            "unit": "ns/iter",
            "extra": "iterations: 1122676\ncpu: 610.4803407216331 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 161.11997214543936,
            "unit": "ns/iter",
            "extra": "iterations: 4354044\ncpu: 161.0973113730592 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 183.27236482889896,
            "unit": "ns/iter",
            "extra": "iterations: 3822285\ncpu: 183.26013523324383 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 205.92700482851865,
            "unit": "ns/iter",
            "extra": "iterations: 3403321\ncpu: 205.90489230959997 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 250.2094821860505,
            "unit": "ns/iter",
            "extra": "iterations: 2722136\ncpu: 250.18849719484996 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3368.5166622478923,
            "unit": "ns/iter",
            "extra": "iterations: 207445\ncpu: 3368.1635373231447 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "f51adebfaed79fd103ffb874783b95c570509f15",
          "message": "feat: add the factorial profile sweeping mutation, crossover and bound\n\nThe sweeps measured generations, population size and the fitness weights, and\nheld everything else at config.hpp's defaults. That left the two core genetic\nknobs — crossover_rate at 0.1 and mutation_rate at 1.0 — never measured at all,\nalong with the per-operator mutation probabilities, the model-counting bound\nand the weakening filter. It also left selection_scheme a constant: every\nconfig pinned nsga2, so results.csv cannot answer whether NSGA-II beats\nweighted, which is the claim the scheme was adopted on.\n\ngen_configs.py now emits sweeps D-J and a finer grid on A and B, once per\nselection scheme, to experiments/configs/<scheme>/. 63 levels per scheme, 126\nconfigs. Each sweep still holds the others at their defaults, so each one's\ndefault level is byte-identical to the A/gen10 baseline and aliases onto it —\nnine per scheme, never across schemes, since the configs differ on\nselection_scheme and the byte-identity check catches it.\n\nselection joins the natural key in run_id, load_done_set and the merge. Without\nthat the two schemes share an output directory and read each other's\nrepair_*.json, resume marks the second scheme already done, and the merge drops\nhalf the rows — three silent successes. Rows predating the column are all\nnsga2, verified against the 1470 per-run config.toml snapshots of the\n2026-07-14 sweep, so both scripts read an absent value as nsga2 and the older\nresults.csv still resumes and merges unchanged.\n\nThe notebook treats selection as a factor rather than pooling the schemes into\none distribution per level, and gains a Fisher/Mann-Whitney comparison between\nthem plus a per-level rate delta, which is what shows a scheme that only wins\nin one corner of the space.\n\nfull is pinned to the 14 levels it has always had so its results.csv stays one\ncomparable dataset. factorial is 50,400 rows from 43,200 executions at 100\nseeds: 58 h serial, ~7.3 h per machine across av2 and av3 at --jobs 4, costed\nfrom the measured A/gen10 baseline of 15.3s per seed across the four specs.\n\ndefault_bound is close to free — it enters through the transfer matrix, not a\nSAT call, and bound 80 measured within noise of bound 5 (1.55s vs 1.53s on fsm)\n— but it moves the result: 4, 3 and 2 repairs at bounds 5, 20 and 80.",
          "timestamp": "2026-07-14T19:40:13+01:00",
          "tree_id": "2fd09d34d1ab38c5922cd5ebcc28f9bd9f98cf88",
          "url": "https://github.com/benmandrew/counter/commit/f51adebfaed79fd103ffb874783b95c570509f15"
        },
        "date": 1784054627451,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 554.0863978057243,
            "unit": "ns/iter",
            "extra": "iterations: 1272405\ncpu: 553.9877334653669 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2263.131272195537,
            "unit": "ns/iter",
            "extra": "iterations: 311155\ncpu: 2262.598341662516 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 615.8959730873474,
            "unit": "ns/iter",
            "extra": "iterations: 1151577\ncpu: 615.7401476410174 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 161.33570118720024,
            "unit": "ns/iter",
            "extra": "iterations: 4356547\ncpu: 161.31390846925336 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 184.21761742126677,
            "unit": "ns/iter",
            "extra": "iterations: 3752195\ncpu: 184.19286497636722 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 206.12933440082392,
            "unit": "ns/iter",
            "extra": "iterations: 3386137\ncpu: 206.0981312923843 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 250.5868402914259,
            "unit": "ns/iter",
            "extra": "iterations: 2740623\ncpu: 250.55578786283283 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3380.403746329401,
            "unit": "ns/iter",
            "extra": "iterations: 206709\ncpu: 3379.9487201815136 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "489502462efbd3f32a8338e78d6e1c19fd803823",
          "message": "Merge pull request #22 from benmandrew/fix/bounded-async-use-after-free\n\nfix: survive a fitness function that throws",
          "timestamp": "2026-07-14T19:42:38+01:00",
          "tree_id": "e4bbb8a7d6ca2d4fada2e372b94fafa5400e0928",
          "url": "https://github.com/benmandrew/counter/commit/489502462efbd3f32a8338e78d6e1c19fd803823"
        },
        "date": 1784054834229,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 552.4391082386315,
            "unit": "ns/iter",
            "extra": "iterations: 1270295\ncpu: 552.3470382863823 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2226.1320272110506,
            "unit": "ns/iter",
            "extra": "iterations: 313549\ncpu: 2225.8126417242597 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 575.6192680714007,
            "unit": "ns/iter",
            "extra": "iterations: 1244411\ncpu: 575.5723655608958 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 168.32265661696977,
            "unit": "ns/iter",
            "extra": "iterations: 4147124\ncpu: 168.31875077764735 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 192.57835387456706,
            "unit": "ns/iter",
            "extra": "iterations: 3619955\ncpu: 192.5541300927774 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 215.43003466236019,
            "unit": "ns/iter",
            "extra": "iterations: 3248480\ncpu: 215.41058525833617 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 263.95419897780533,
            "unit": "ns/iter",
            "extra": "iterations: 2647823\ncpu: 263.8922707446835 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3380.00701925583,
            "unit": "ns/iter",
            "extra": "iterations: 207002\ncpu: 3379.814185370191 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "52167cb91e1207ab409924272988e068a88ca1af",
          "message": "feat: record dropped individuals in the results CSV\n\n193e3dd made counter drop an individual whose fitness scoring throws instead\nof aborting the run, and print a scoring report on the way out — its comment\nputs the reason plainly: a silent drop must never be mistaken for a clean\nsweep. The experiment CSV had no column for it, so at the dataset level the\ndrop was silent anyway. A run that lost 5% of every generation to a failing\nexternal tool produced a row indistinguishable from one that lost nothing.\n\nn_dropped parses the count out of the run log, mirroring timed_out. The report\nis silent when nothing was dropped, so an absent match is a genuine zero rather\nthan a parse failure. Filtering on n_dropped == 0 now gives a strictly clean\ndataset, and a non-zero column is the signal that the tooling, not the search,\nis what moved a result.\n\nLanding this before the factorial profile's 50,400 rows rather than after: the\ncolumn cannot be recovered from a CSV that never carried it, only from 43,200\nindividual run logs.",
          "timestamp": "2026-07-14T19:46:16+01:00",
          "tree_id": "f3273bb3d84977032525ef6738ee096e0b5a8b17",
          "url": "https://github.com/benmandrew/counter/commit/52167cb91e1207ab409924272988e068a88ca1af"
        },
        "date": 1784054999339,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 561.255069079005,
            "unit": "ns/iter",
            "extra": "iterations: 1278536\ncpu: 561.1980390071145 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2190.579437734452,
            "unit": "ns/iter",
            "extra": "iterations: 320169\ncpu: 2190.502278484176 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 582.0906906318246,
            "unit": "ns/iter",
            "extra": "iterations: 1234681\ncpu: 581.9983599002496 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 167.87450475925652,
            "unit": "ns/iter",
            "extra": "iterations: 4165146\ncpu: 167.8669792127335 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 190.74187278549434,
            "unit": "ns/iter",
            "extra": "iterations: 3664755\ncpu: 190.7268848804353 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 213.02660161916285,
            "unit": "ns/iter",
            "extra": "iterations: 3282319\ncpu: 213.0106503968687 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 263.4771902041376,
            "unit": "ns/iter",
            "extra": "iterations: 2676109\ncpu: 263.44306491252775 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3315.2205161571997,
            "unit": "ns/iter",
            "extra": "iterations: 210556\ncpu: 3314.991930887745 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "58b77aac086cdf660914433d6cba19a1f54a3d00",
          "message": "fix: derive the merge sort key from KEY_FIELDS\n\nAdding `selection` to KEY_FIELDS widened the natural key to five fields, but\nsort_key still destructured four of them, so merge_csv raised ValueError: too\nmany values to unpack (expected 4) on every merge. The failure landed after\nrsync had already pulled 2.2GB, making it look like a transfer problem rather\nthan an arity mismatch.\n\nThe seed index now comes from KEY_FIELDS.index(\"seed\"), so a sixth key field\ncannot silently break the arity again. Seeds still sort numerically and\nnon-numeric seeds sort after them rather than raising.\n\nVerified against the factorial run: 50,393 rows merged from av2 and av3 with\n0 duplicate keys, seeds 0-99.",
          "timestamp": "2026-07-15T13:07:14+01:00",
          "tree_id": "5ae1e090b3db4638152fbbdbe3eb4971d6722229",
          "url": "https://github.com/benmandrew/counter/commit/58b77aac086cdf660914433d6cba19a1f54a3d00"
        },
        "date": 1784117623554,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 554.4852144224702,
            "unit": "ns/iter",
            "extra": "iterations: 1281722\ncpu: 554.4018055397348 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2182.9268603604683,
            "unit": "ns/iter",
            "extra": "iterations: 312211\ncpu: 2181.9847955389146 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 564.0833573527069,
            "unit": "ns/iter",
            "extra": "iterations: 1238583\ncpu: 564.0412875035424 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 176.8171524750887,
            "unit": "ns/iter",
            "extra": "iterations: 4163644\ncpu: 176.81233337912644 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 192.09766732416068,
            "unit": "ns/iter",
            "extra": "iterations: 3640583\ncpu: 192.0809625820919 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 218.8159492449827,
            "unit": "ns/iter",
            "extra": "iterations: 3259264\ncpu: 218.79807036189789 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 265.19690428251835,
            "unit": "ns/iter",
            "extra": "iterations: 2488793\ncpu: 265.1545423825928 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3270.9725368422637,
            "unit": "ns/iter",
            "extra": "iterations: 212867\ncpu: 3270.784489845774 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "a37a911b164585296ec6f6883d545d4b96f9746c",
          "message": "fix: stop signal_tracer leaking a process per concurrent crash\n\ncounter faults inside the std::async scoring pool, so several worker threads\nenter crash_handler at once. Each one ran pipe() + fork() + execl(signal_tracer),\nand pipe() leaves the descriptors without close-on-exec. A concurrent handler's\nfork() therefore copied a sibling's pipe write end into its own tracer, where it\nsurvived execl and held the pipe open. The sibling's tracer never read EOF, its\nfread loop blocked forever, and the handler's waitpid blocked on a child that\ncould not exit.\n\nThe tracers outlived the process that forked them, reparented to init, and\naccumulated: 111 across three machines (51 local, 23 on av2, 37 on av3), the\noldest alive for 1 day 20 hours, holding 226MB of resident memory on the two\nremote machines alone. One tracer's stdin pipe had 13 write-end copies spread\nacross 7 sibling tracers.\n\npipe2(O_CLOEXEC) closes those stray copies at execl, so the pipe reaches EOF as\nsoon as the real writer closes it. The flag is set atomically because pipe()\nfollowed by fcntl() would leave a window for a concurrent fork() to race, which\nis the failure being fixed. dup2 onto stdin clears close-on-exec, so the tracer\nkeeps its input.\n\nAn atomic_flag guard now admits one thread to the reporting path and parks the\nrest until it _exits, which keeps one crash to one log and one tracer rather\nthan one per faulting thread.\n\nVerified with a standalone reproduction of the same pipe/fork/exec shape:\nreaders deadlock on pipe(), exit cleanly on pipe2(O_CLOEXEC).",
          "timestamp": "2026-07-15T13:18:43+01:00",
          "tree_id": "4fb031824e3f3cb147231057851150860c837920",
          "url": "https://github.com/benmandrew/counter/commit/a37a911b164585296ec6f6883d545d4b96f9746c"
        },
        "date": 1784118151272,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 455.5954512272343,
            "unit": "ns/iter",
            "extra": "iterations: 1567104\ncpu: 455.55061565792704 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1762.5879805498864,
            "unit": "ns/iter",
            "extra": "iterations: 396707\ncpu: 1762.4035446815906 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 452.70460973206553,
            "unit": "ns/iter",
            "extra": "iterations: 1549743\ncpu: 452.6623078794355 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 129.77976590773642,
            "unit": "ns/iter",
            "extra": "iterations: 5387363\ncpu: 129.76670812789118 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 148.65577612424931,
            "unit": "ns/iter",
            "extra": "iterations: 4520436\ncpu: 148.64309681632486 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 164.7191553305322,
            "unit": "ns/iter",
            "extra": "iterations: 4213151\ncpu: 164.7047831895891 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 200.020565627359,
            "unit": "ns/iter",
            "extra": "iterations: 3408746\ncpu: 199.9973855488207 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2589.0154425584155,
            "unit": "ns/iter",
            "extra": "iterations: 270292\ncpu: 2588.492238024063 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "587a5b64fa46df6dbc5dec2c63baa8f6a1342d55",
          "message": "feat: default the selection scheme to nsga2\n\nWeightedAverage was the shipped default in both Config and\nexample-config.toml, and it is the weakest setting available. Over a\n50,393-run parameter sweep across four example specifications, both\nschemes, and ten swept parameters:\n\n  - takeoff:      1.7% of runs matched an ideal repair under weighted,\n                  89.3% under nsga2 (Fisher p ~ 0)\n  - fsm-timing:  86.6% vs 95.6% (p = 5.8e-72)\n  - fsm:          0.7% vs  4.4% (p = 4.9e-43)\n\nIt costs nothing: nsga2's median wall-clock time is at or below\nweighted's on every specification (takeoff 3.06s vs 3.25s; fsm-timing\n3.62s vs 4.07s).\n\nWeighted converges prematurely and then stagnates. Its results do not\nmove with the generation count at any level from 5 to 80 -- fsm-timing\nsits flat at 0.94 and takeoff at 0.00 across a 16x budget increase --\nand across 800 runs it produces only 9 distinct fitness values, so the\nseed barely matters. nsga2 keeps improving with budget.\n\nWeighted is retained for comparison rather than removed.\n\nTwo generation tests took the default implicitly while covering\ntruncation-selection behaviour, and now pin WeightedAverage: padding a\nfiltered generation back to size, and elitism keeping a filtered-out top\nspec alive. NSGA-II reaches neither -- its (mu+lambda) survivor pooling\nrefills from the parents and is already elitist, which is why\nelitism_rate = 0 is its natural companion setting.",
          "timestamp": "2026-07-15T17:35:58+01:00",
          "tree_id": "ce0e91ea9237fe407118a1ed1cd8e66b736089cc",
          "url": "https://github.com/benmandrew/counter/commit/587a5b64fa46df6dbc5dec2c63baa8f6a1342d55"
        },
        "date": 1784134626547,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 550.2164458276487,
            "unit": "ns/iter",
            "extra": "iterations: 1285250\ncpu: 550.1730986189457 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2357.7642756187192,
            "unit": "ns/iter",
            "extra": "iterations: 290793\ncpu: 2357.485916098393 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 592.5914103348398,
            "unit": "ns/iter",
            "extra": "iterations: 1152315\ncpu: 592.5419603146709 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 163.17352717646375,
            "unit": "ns/iter",
            "extra": "iterations: 4165774\ncpu: 163.16312670826593 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 186.1762041877402,
            "unit": "ns/iter",
            "extra": "iterations: 3753526\ncpu: 186.1623702619882 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 209.16798362497394,
            "unit": "ns/iter",
            "extra": "iterations: 3352422\ncpu: 209.13262888741312 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 255.2384435069302,
            "unit": "ns/iter",
            "extra": "iterations: 2754577\ncpu: 255.23300673751356 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3427.5317712528463,
            "unit": "ns/iter",
            "extra": "iterations: 204855\ncpu: 3426.8997388396647 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "9469e158e0e641ba3922d4b1a343c64f1deef958",
          "message": "Merge pull request #23 from benmandrew/experiments/cj-large-weakening\n\nCross the weakening filter with sweeps C–I and fix the cj-large merge key",
          "timestamp": "2026-07-16T14:04:28+01:00",
          "tree_id": "3dbdba44d8735e1e6a2204f290245e9ef0aab3bc",
          "url": "https://github.com/benmandrew/counter/commit/9469e158e0e641ba3922d4b1a343c64f1deef958"
        },
        "date": 1784207301011,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 548.1095490195087,
            "unit": "ns/iter",
            "extra": "iterations: 1287661\ncpu: 548.0739860879532 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2261.7969958448975,
            "unit": "ns/iter",
            "extra": "iterations: 310703\ncpu: 2261.655252765502 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 611.7626405892645,
            "unit": "ns/iter",
            "extra": "iterations: 1145002\ncpu: 611.7105673177862 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 163.98786121343886,
            "unit": "ns/iter",
            "extra": "iterations: 4287908\ncpu: 163.96434881531982 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 186.94219703521512,
            "unit": "ns/iter",
            "extra": "iterations: 3754669\ncpu: 186.93567155986307 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 209.28071850505827,
            "unit": "ns/iter",
            "extra": "iterations: 3347339\ncpu: 209.25663848208976 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 254.6444927577284,
            "unit": "ns/iter",
            "extra": "iterations: 2748726\ncpu: 254.63540527502568 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3405.6687321235854,
            "unit": "ns/iter",
            "extra": "iterations: 205927\ncpu: 3405.193330646299 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "0ab96ad6c497af20346aab5ae069fe73c1338f29",
          "message": "Merge pull request #24 from benmandrew/feat/long-double-count\n\nfeat: represent model counts as long double",
          "timestamp": "2026-07-16T16:04:50+01:00",
          "tree_id": "af2b9a39b0f4e070f551f3cc5b0299579ab4805b",
          "url": "https://github.com/benmandrew/counter/commit/0ab96ad6c497af20346aab5ae069fe73c1338f29"
        },
        "date": 1784214674856,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 567.1411050252851,
            "unit": "ns/iter",
            "extra": "iterations: 1242415\ncpu: 566.9636248757462 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2326.9799970716576,
            "unit": "ns/iter",
            "extra": "iterations: 300506\ncpu: 2326.0256533979345 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 601.960533151137,
            "unit": "ns/iter",
            "extra": "iterations: 1129961\ncpu: 601.7615643371761 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 197.67230386692984,
            "unit": "ns/iter",
            "extra": "iterations: 3539523\ncpu: 197.61713739393696 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 223.35330355199034,
            "unit": "ns/iter",
            "extra": "iterations: 3142012\ncpu: 223.28196168569693 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 249.21826692440257,
            "unit": "ns/iter",
            "extra": "iterations: 2809537\ncpu: 249.14493348904102 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 299.6355347087274,
            "unit": "ns/iter",
            "extra": "iterations: 2335767\ncpu: 299.578771341491 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3481.552347193797,
            "unit": "ns/iter",
            "extra": "iterations: 196916\ncpu: 3481.0488126917044 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "24baa90e6ab21afd8833f5101f202bfa55cbdfe1",
          "message": "Merge pull request #25 from benmandrew/feat/config-similarity-metric\n\nfeat: select the semantic-similarity metric from config",
          "timestamp": "2026-07-16T16:25:40+01:00",
          "tree_id": "1dc9493c65a60f0acd8b4a44ecb89424f09d8bf2",
          "url": "https://github.com/benmandrew/counter/commit/24baa90e6ab21afd8833f5101f202bfa55cbdfe1"
        },
        "date": 1784215811720,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 563.8207715905085,
            "unit": "ns/iter",
            "extra": "iterations: 1246231\ncpu: 563.7693517493949 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2275.372159247281,
            "unit": "ns/iter",
            "extra": "iterations: 308677\ncpu: 2275.0313531620436 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 617.7521739362536,
            "unit": "ns/iter",
            "extra": "iterations: 1122618\ncpu: 617.6780258289108 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 197.75517625839012,
            "unit": "ns/iter",
            "extra": "iterations: 3542810\ncpu: 197.73374863455848 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 229.49369362309224,
            "unit": "ns/iter",
            "extra": "iterations: 3139362\ncpu: 229.4636728736605 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 250.21080207732075,
            "unit": "ns/iter",
            "extra": "iterations: 2816125\ncpu: 250.17711536242172 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 308.7217158507736,
            "unit": "ns/iter",
            "extra": "iterations: 2333816\ncpu: 308.3863646491412 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3349.934017153141,
            "unit": "ns/iter",
            "extra": "iterations: 208827\ncpu: 3349.502607421455 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "18865cc7da9fffa9bfe1ce30f02fddc12a5010ee",
          "message": "Merge pull request #26 from benmandrew/feat/tlsf-parity\n\nBring the TLSF repair path to feature parity with FRETISH",
          "timestamp": "2026-07-16T17:43:50+01:00",
          "tree_id": "b319567f00ca1f5f4f51428edf18dd3c8870ac0c",
          "url": "https://github.com/benmandrew/counter/commit/18865cc7da9fffa9bfe1ce30f02fddc12a5010ee"
        },
        "date": 1784220478268,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 417.4618036235676,
            "unit": "ns/iter",
            "extra": "iterations: 1675028\ncpu: 417.41053403286384 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1690.9977019855794,
            "unit": "ns/iter",
            "extra": "iterations: 423409\ncpu: 1690.8362765080567 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 436.6598804819935,
            "unit": "ns/iter",
            "extra": "iterations: 1600261\ncpu: 436.62952355896937 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 159.77156486212652,
            "unit": "ns/iter",
            "extra": "iterations: 4425834\ncpu: 159.76487437170033 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 177.62803725328405,
            "unit": "ns/iter",
            "extra": "iterations: 3957235\ncpu: 177.6085041702097 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 198.319700712434,
            "unit": "ns/iter",
            "extra": "iterations: 3521162\ncpu: 198.3117791797141 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 235.59490207398042,
            "unit": "ns/iter",
            "extra": "iterations: 2927465\ncpu: 235.48736022463137 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2639.7634159547097,
            "unit": "ns/iter",
            "extra": "iterations: 265952\ncpu: 2639.049873661413 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "81c8462df966aa3c8246845ded78301f5064c521",
          "message": "fix: silence -Wpedantic on the __int128 rounding-reference in transfer_matrix_tests\n\ntest_count_traces_rounds_faithfully_above_64_bits holds its exact reference in\nunsigned __int128 -- the wider type the float Count deliberately no longer uses\n-- to pin that a count past 2^64 rounds faithfully. Under -Werror=pedantic the\nbare `unsigned __int128` declaration trips \"ISO C++ does not support __int128\"\nand fails the counter_tests build, so `cmake --workflow --preset release`\n(build + test) never completed even though counter/compare built fine.\n\nPrefix the declaration with __extension__, the GCC/Clang keyword that accepts\nthe extension without the pedantic diagnostic. The block is already guarded by\n#ifdef __SIZEOF_INT128__, so no new portability surface. Full suite: 30/30.",
          "timestamp": "2026-07-16T17:57:20+01:00",
          "tree_id": "6693b0a404c471c5799dd78455c8ba5ef80843f2",
          "url": "https://github.com/benmandrew/counter/commit/81c8462df966aa3c8246845ded78301f5064c521"
        },
        "date": 1784221361400,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 541.18625508835,
            "unit": "ns/iter",
            "extra": "iterations: 1306418\ncpu: 541.1466307108445 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2132.5071387674097,
            "unit": "ns/iter",
            "extra": "iterations: 328348\ncpu: 2132.2286415632193 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 605.7290208576732,
            "unit": "ns/iter",
            "extra": "iterations: 1195771\ncpu: 605.6796869969249 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 202.30404481258006,
            "unit": "ns/iter",
            "extra": "iterations: 3445104\ncpu: 202.29934190665935 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 229.00046800150105,
            "unit": "ns/iter",
            "extra": "iterations: 3053409\ncpu: 228.98090003664745 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 254.1529352387282,
            "unit": "ns/iter",
            "extra": "iterations: 2748124\ncpu: 254.14219773198008 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 305.9387375645329,
            "unit": "ns/iter",
            "extra": "iterations: 2287911\ncpu: 305.9231543534695 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3355.35692378189,
            "unit": "ns/iter",
            "extra": "iterations: 213886\ncpu: 3355.3442067269507 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "865b4dc915f3673812f7579892a4c56f755d4375",
          "message": "feat: default the similarity metric to logarithmic\n\nThe direct-vs-log campaign (metric profile, 800 runs at generations=40/\npopulation=1000) found logarithmic recovers more ideal repairs across every\nspec -- overall implies-ideal 68.8% vs 61.5%, decisively on fsm (66% vs 40%,\nFisher p=3.7e-4) -- and is never worse. Flip Config::similarity_metric's default\nfrom Direct to Logarithmic so a config omitting model_counting.metric now scores\nlogarithmically; production reaches it through the Config overload, which passes\ncfg.similarity_metric, so no call site changes.\n\nThe low-level semantic_similarity/…_from_counts overloads keep their metric\ndefault argument at Direct: those are test-facing entry points and their\ndirect-formula unit tests (e.g. the 6/7 case) assert that metric's arithmetic,\nso Direct stays the natural fixture there. The one test that compared the Config\noverload against a bound-only call now pins the metric too, since Config carries\nboth the bound and the metric.\n\ngen_configs.py's DEFAULTS keeps metric = \"direct\": a flat (non-crossed) config\nhas no metric directory, so run_experiments.py's metric_of() records it as\nLEGACY_METRIC (\"direct\"); pinning direct there keeps the emitted value matching\nthe recorded CSV column and past grids comparable. Cross --metric to exercise\nlogarithmic. example-config.toml now shows logarithmic as the default.",
          "timestamp": "2026-07-16T18:12:13+01:00",
          "tree_id": "c967eac0912e9c6fa14ececa15ae15d1630258a3",
          "url": "https://github.com/benmandrew/counter/commit/865b4dc915f3673812f7579892a4c56f755d4375"
        },
        "date": 1784222389799,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 539.0817092087509,
            "unit": "ns/iter",
            "extra": "iterations: 1302240\ncpu: 538.9987552217718 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2185.5710189292295,
            "unit": "ns/iter",
            "extra": "iterations: 320140\ncpu: 2185.2905385144 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 571.8778443291078,
            "unit": "ns/iter",
            "extra": "iterations: 1216104\ncpu: 571.809101030833 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 208.7850228448121,
            "unit": "ns/iter",
            "extra": "iterations: 3450455\ncpu: 208.76256058983535 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 230.4204807613564,
            "unit": "ns/iter",
            "extra": "iterations: 3003902\ncpu: 230.38780459548946 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 259.84761357226796,
            "unit": "ns/iter",
            "extra": "iterations: 2755206\ncpu: 259.8246131142284 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 315.1366985354956,
            "unit": "ns/iter",
            "extra": "iterations: 2209892\ncpu: 315.0747068182517 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3257.746990482445,
            "unit": "ns/iter",
            "extra": "iterations: 214237\ncpu: 3257.2145054309008 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "aee5322ca396d553056f83babb08dfc127254018",
          "message": "Merge pull request #27 from benmandrew/fix/tlsf-parser-conformance\n\nfix: make the TLSF parser and lowering conform to syfco/TLSF v1.1",
          "timestamp": "2026-07-16T18:51:34+01:00",
          "tree_id": "84d8962e94b44c45f0124a020b42e636170348a3",
          "url": "https://github.com/benmandrew/counter/commit/aee5322ca396d553056f83babb08dfc127254018"
        },
        "date": 1784224519254,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 532.114511326977,
            "unit": "ns/iter",
            "extra": "iterations: 1319328\ncpu: 532.0654992541657 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2212.9044216153916,
            "unit": "ns/iter",
            "extra": "iterations: 317237\ncpu: 2212.708041621879 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 634.9187711097259,
            "unit": "ns/iter",
            "extra": "iterations: 1110270\ncpu: 634.8517531771548 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 197.01254191452665,
            "unit": "ns/iter",
            "extra": "iterations: 3529206\ncpu: 196.9956695642023 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 222.6751924768008,
            "unit": "ns/iter",
            "extra": "iterations: 3142067\ncpu: 222.66350081013556 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 248.45056321866565,
            "unit": "ns/iter",
            "extra": "iterations: 2817467\ncpu: 248.44297768172612 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 299.8253258485386,
            "unit": "ns/iter",
            "extra": "iterations: 2337896\ncpu: 299.8145050079215 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3370.110526162371,
            "unit": "ns/iter",
            "extra": "iterations: 205716\ncpu: 3369.884938458844 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "390d30a830de466d55937deafbb4d0e25b373ed2",
          "message": "ci: run the benchmark job in parallel with build-and-test\n\nDrop `needs: build-and-test` from the benchmark job. It shares no data\nwith build-and-test (there are no `needs.*` references) and keeps its own\ndependency cache, so the edge only serialised two otherwise independent\njobs. Removing it lets benchmark start immediately alongside check and\nbuild-and-test.",
          "timestamp": "2026-07-16T18:54:22+01:00",
          "tree_id": "baa34803b72e99f49526feb1fd42994ada01603d",
          "url": "https://github.com/benmandrew/counter/commit/390d30a830de466d55937deafbb4d0e25b373ed2"
        },
        "date": 1784224577916,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 358.90670156865497,
            "unit": "ns/iter",
            "extra": "iterations: 1806965\ncpu: 358.8859125660984 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1411.001926715906,
            "unit": "ns/iter",
            "extra": "iterations: 493586\ncpu: 1410.926341103678 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 389.5197063672794,
            "unit": "ns/iter",
            "extra": "iterations: 1757706\ncpu: 389.50324229421756 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 124.83691197965678,
            "unit": "ns/iter",
            "extra": "iterations: 5614134\ncpu: 124.82908583941882 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 139.61261707446835,
            "unit": "ns/iter",
            "extra": "iterations: 5011447\ncpu: 139.60873037268476 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 156.8172346901486,
            "unit": "ns/iter",
            "extra": "iterations: 4473524\ncpu: 156.8076959014863 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 191.26219592423627,
            "unit": "ns/iter",
            "extra": "iterations: 3670919\ncpu: 191.20273152308715 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2208.023366803708,
            "unit": "ns/iter",
            "extra": "iterations: 288315\ncpu: 2207.1866014602056 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "d9ae0ea8e62e5325b3135fa319f72bfd5312412f",
          "message": "feat: add TLSF examples and rename arbiter-gr1 to arbiter\n\nAdd per-scenario TLSF example directories (gyro-var1, humanoid-531,\nlift, lily02, minepump), each with a spec.tlsf and fixes/. Rename the\narbiter-gr1 scenario to arbiter, moving its ideal to a named fix under\nfixes/.",
          "timestamp": "2026-07-16T19:00:21+01:00",
          "tree_id": "d28beca99120d42501cee975698d19d496f4dba6",
          "url": "https://github.com/benmandrew/counter/commit/d9ae0ea8e62e5325b3135fa319f72bfd5312412f"
        },
        "date": 1784224991892,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 576.5357254325431,
            "unit": "ns/iter",
            "extra": "iterations: 1063724\ncpu: 576.4584459878689 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2494.08010929407,
            "unit": "ns/iter",
            "extra": "iterations: 293886\ncpu: 2493.3800010888576 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 604.6462855769734,
            "unit": "ns/iter",
            "extra": "iterations: 1152817\ncpu: 604.5445200756059 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 197.6361578704583,
            "unit": "ns/iter",
            "extra": "iterations: 3203994\ncpu: 197.60322366396437 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 222.8591102920759,
            "unit": "ns/iter",
            "extra": "iterations: 3137007\ncpu: 222.8280905971839 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 248.90475796261092,
            "unit": "ns/iter",
            "extra": "iterations: 2645397\ncpu: 248.90009136624866 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 299.83578317961025,
            "unit": "ns/iter",
            "extra": "iterations: 2334024\ncpu: 299.810238026687 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3404.683008167589,
            "unit": "ns/iter",
            "extra": "iterations: 206305\ncpu: 3404.4356365575245 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "1d7a56a8eeefb50e59f516781c86bf08fb9cb185",
          "message": "Merge pull request #29 from benmandrew/feat/ltl2tgba-tautology-substitution\n\nfeat: treat ltl2tgba's exit-2-on-tautology as trivially true",
          "timestamp": "2026-07-17T15:29:53+01:00",
          "tree_id": "6c33c875acf44fa33c68f0592506a4ca2d516c4e",
          "url": "https://github.com/benmandrew/counter/commit/1d7a56a8eeefb50e59f516781c86bf08fb9cb185"
        },
        "date": 1784298694451,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 427.5936554737696,
            "unit": "ns/iter",
            "extra": "iterations: 1623888\ncpu: 427.393838121841 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1713.3550340651013,
            "unit": "ns/iter",
            "extra": "iterations: 413473\ncpu: 1712.687370154762 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 436.0096321354801,
            "unit": "ns/iter",
            "extra": "iterations: 1589471\ncpu: 435.9747400235675 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 156.55969612586242,
            "unit": "ns/iter",
            "extra": "iterations: 4473431\ncpu: 156.54792350658823 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 177.66735966683396,
            "unit": "ns/iter",
            "extra": "iterations: 3970177\ncpu: 177.58638468763488 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 198.42333077793285,
            "unit": "ns/iter",
            "extra": "iterations: 3567605\ncpu: 198.32418134855172 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 238.15360241084386,
            "unit": "ns/iter",
            "extra": "iterations: 2959296\ncpu: 238.06903162103424 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2581.4267150017504,
            "unit": "ns/iter",
            "extra": "iterations: 270335\ncpu: 2581.09178981634 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "8bc284b678b6d41942edcb896b9d551e6cad0205",
          "message": "Merge pull request #30 from benmandrew/feat/muc-repair\n\nMUC extraction and MUC-guided TLSF repair",
          "timestamp": "2026-07-17T19:21:01+01:00",
          "tree_id": "46f70bb2dd47f5169e67a0dcccdfbb1b92a58b80",
          "url": "https://github.com/benmandrew/counter/commit/8bc284b678b6d41942edcb896b9d551e6cad0205"
        },
        "date": 1784312567230,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 561.5902185018633,
            "unit": "ns/iter",
            "extra": "iterations: 1279211\ncpu: 561.5292723405288 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2179.8531176070055,
            "unit": "ns/iter",
            "extra": "iterations: 320406\ncpu: 2179.6172543585335 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 569.5607002691991,
            "unit": "ns/iter",
            "extra": "iterations: 1210563\ncpu: 569.483941769243 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 202.88521268171922,
            "unit": "ns/iter",
            "extra": "iterations: 3482998\ncpu: 202.86901686420723 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 230.38847620946922,
            "unit": "ns/iter",
            "extra": "iterations: 3034505\ncpu: 230.3676118510267 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 255.44155299118555,
            "unit": "ns/iter",
            "extra": "iterations: 2706828\ncpu: 255.40870938234715 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 312.2055650974487,
            "unit": "ns/iter",
            "extra": "iterations: 2261955\ncpu: 312.14083082996785 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3341.6260271445544,
            "unit": "ns/iter",
            "extra": "iterations: 211387\ncpu: 3341.219909455171 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "Ben Andrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "8a9adc5329f76780b2d1207cd922907f1b3b9967",
          "message": "Merge pull request #28 from benmandrew/feat/tlsf-parameter-sweep\n\nfeat: TLSF parameter-sweep experiment support",
          "timestamp": "2026-07-17T19:32:20+01:00",
          "tree_id": "764753a7348bac82e51aa84da425f2897753011d",
          "url": "https://github.com/benmandrew/counter/commit/8a9adc5329f76780b2d1207cd922907f1b3b9967"
        },
        "date": 1784313257576,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 556.6832697924275,
            "unit": "ns/iter",
            "extra": "iterations: 1244605\ncpu: 556.6394510708218 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2210.2622454890065,
            "unit": "ns/iter",
            "extra": "iterations: 317668\ncpu: 2210.112797008197 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 580.8263928944078,
            "unit": "ns/iter",
            "extra": "iterations: 1234408\ncpu: 580.7584242811129 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 201.8521523817938,
            "unit": "ns/iter",
            "extra": "iterations: 3440096\ncpu: 201.83623480274957 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 226.66464363206,
            "unit": "ns/iter",
            "extra": "iterations: 3093867\ncpu: 226.64674887446688 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 254.00434268168684,
            "unit": "ns/iter",
            "extra": "iterations: 2750144\ncpu: 253.99047104442536 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 305.4044196812303,
            "unit": "ns/iter",
            "extra": "iterations: 2265367\ncpu: 305.3794126956031 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3305.407378583941,
            "unit": "ns/iter",
            "extra": "iterations: 212019\ncpu: 3304.9078101490904 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "a373c14130e544c192ecdb6e5cecfe7419a874d9",
          "message": "feat: add TLSF mono-vs-muc mutation-split campaign (profile muc, sweep M)\n\nRework the muc profile from a single all-defaults level into repair_mode\n{mono, muc} crossed with a new TLSF assumption/guarantee mutation split\n(sweep M: p_guarantee in 0.3/0.5/0.7/0.9, p_assumption its complement).\n\nThe split interacts with repair_mode without confounding it: muc keeps the\nenvironment side at full size while shrinking the guarantee side to the\nminimal core, so at a fixed p_guarantee it spends more of its guarantee\nmutations on the culprit formulae, while the assumption side stays live in\nboth arms. Responses are implies_ideal and wall_time_s co-equal.\n\ngen_configs.py: add the p_assumption/p_guarantee/p_temporal keys, emit a\n[tlsf.mutation] section only when a sweep overrides one of them (the FRETISH\nand A/B TLSF grids stay byte-identical), define sweep M, and widen --sweeps\nto accept TLSF-only sweeps while keeping them out of the default set.\n\nrun_experiments.py: point the muc profile at sweep M, size the timeout caps\nto the slow monolithic arm so it is never censored (humanoid 2400s, from a\nmeasured 768s mean / 1739s p90), and drop default_jobs to 1 so the\nper-process ltlsynt RAM cap stays machine-wide.",
          "timestamp": "2026-07-17T19:54:35+01:00",
          "tree_id": "8016cb2754b271c1fd613f7f3d548aeee668ee90",
          "url": "https://github.com/benmandrew/counter/commit/a373c14130e544c192ecdb6e5cecfe7419a874d9"
        },
        "date": 1784314599688,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 547.9303482933897,
            "unit": "ns/iter",
            "extra": "iterations: 1290837\ncpu: 547.8816698002923 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2269.2142265166576,
            "unit": "ns/iter",
            "extra": "iterations: 311306\ncpu: 2269.0651481179293 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 627.4247397466826,
            "unit": "ns/iter",
            "extra": "iterations: 1135048\ncpu: 627.3594843566085 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 197.73633834193205,
            "unit": "ns/iter",
            "extra": "iterations: 3539047\ncpu: 197.71725806410595 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 223.29097136756369,
            "unit": "ns/iter",
            "extra": "iterations: 3139656\ncpu: 223.27004200460192 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 249.09731773510498,
            "unit": "ns/iter",
            "extra": "iterations: 2811430\ncpu: 249.0786649498656 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 300.1654010801619,
            "unit": "ns/iter",
            "extra": "iterations: 2332252\ncpu: 300.14313676223657 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3474.0506263593593,
            "unit": "ns/iter",
            "extra": "iterations: 200923\ncpu: 3473.670858985783 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "committer": {
            "email": "benmandrew@gmail.com",
            "name": "benmandrew",
            "username": "benmandrew"
          },
          "distinct": true,
          "id": "62bbc6f1613b9d7a68912b84fa01996aa3cdd670",
          "message": "fix: bound and reap the ltl2tgba model-counting subprocess\n\nThe model-counting path (run_ltl2tgba_for_counting) had two defects that\ntogether leaked multi-GB processes across long TLSF campaigns.\n\n1. No timeout. It exec'd `ltl2tgba -D -S -H` via execute_and_capture with no\n   timeout, unlike the realizability path which passes ltlsynt_timeout_ms and\n   kills on the deadline. On the deeply nested formulae the search builds, the\n   -D determinization blows up super-exponentially and runs unbounded (observed:\n   40+ GB, hours). Add a Config::ltl2tgba_timeout (runtime.ltl2tgba_timeout_ms,\n   0 = disabled, matching ltlsynt_timeout), thread it into the counting exec, and\n   on timeout drop the individual via the existing max_scoring_failure_rate\n   circuit breaker rather than caching a partial result or stalling the run.\n\n2. No death-linkage. spawn_child_and_exec did only dup2 + execv, so when the\n   parent counter process was killed (e.g. a campaign harness enforcing a\n   wall/RAM budget) while a subprocess was mid-run, the child was reparented to\n   PID 1 and never reaped. Add prctl(PR_SET_PDEATHSIG, SIGKILL) after fork, with\n   a getppid() race guard. This covers every subprocess (ltl2tgba, ltlsynt,\n   black), all of which are short-lived queries that should never outlive the\n   parent.\n\nVerified: unit tests pass; a benign 10s timeout runs normally (9 ltl2tgba calls,\nexit 0) while a 1ms timeout trips the counting-timeout path and the scoring\ncircuit breaker as designed.",
          "timestamp": "2026-07-20T14:16:07+01:00",
          "tree_id": "226fd2610d45b0eb3e19f5919cd895663a3867e7",
          "url": "https://github.com/benmandrew/counter/commit/62bbc6f1613b9d7a68912b84fa01996aa3cdd670"
        },
        "date": 1784553582221,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 542.4290360585136,
            "unit": "ns/iter",
            "extra": "iterations: 1297807\ncpu: 542.3433607616541 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2127.1059847984857,
            "unit": "ns/iter",
            "extra": "iterations: 328783\ncpu: 2125.9217508204506 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 588.1716951302956,
            "unit": "ns/iter",
            "extra": "iterations: 1204344\ncpu: 587.7879551025286 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 202.51635185132267,
            "unit": "ns/iter",
            "extra": "iterations: 3456153\ncpu: 202.47840532522721 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 230.62887756668326,
            "unit": "ns/iter",
            "extra": "iterations: 3042791\ncpu: 230.5751673381446 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 256.29622211235625,
            "unit": "ns/iter",
            "extra": "iterations: 2756911\ncpu: 256.2476811910143 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 307.7511057273905,
            "unit": "ns/iter",
            "extra": "iterations: 2286730\ncpu: 307.73408054295845 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3301.963572793282,
            "unit": "ns/iter",
            "extra": "iterations: 212506\ncpu: 3301.669148165227 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "id": "60d97180a851d4bdd875e20113557f6471d2da88",
          "message": "ci: skip the check job's Nix work when nothing needs checking\n\nThe check job installed Nix, evaluated the flake, and ran cmake configure\nbefore discovering that a pull request had touched no C/C++ or docs files and\nthere was nothing to lint or build. On such a run that setup was the entire\ncost (~1m30s of install, flake eval, and configure to do nothing).\n\nMove the git-only change detection ahead of the Nix install, and gate the Nix\ninstall, the caches, and the configure/lint/docs step on a new \"heavy\" output:\ntrue only when a C/C++ file changed (there is something to lint) or a docs\ninput changed (there is something to rebuild), and always true on the nightly\nschedule and manual dispatch. When neither changed, every heavy step is skipped\nand the job finishes in seconds without touching Nix. The job still runs and\nreports success, so it remains usable as a required status check.",
          "timestamp": "2026-07-20T14:25:37Z",
          "url": "https://github.com/benmandrew/counter/commit/60d97180a851d4bdd875e20113557f6471d2da88"
        },
        "date": 1784612915423,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 467.52742628537527,
            "unit": "ns/iter",
            "extra": "iterations: 1349800\ncpu: 467.4280056304639 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1805.6010294829412,
            "unit": "ns/iter",
            "extra": "iterations: 392430\ncpu: 1805.0754580434725 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 513.5230879999995,
            "unit": "ns/iter",
            "extra": "iterations: 1000000\ncpu: 513.4169330000002 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 159.07485453930732,
            "unit": "ns/iter",
            "extra": "iterations: 4333645\ncpu: 159.07434434523356 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 168.47352792931284,
            "unit": "ns/iter",
            "extra": "iterations: 4065209\ncpu: 168.46416629501704 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 194.89881890369648,
            "unit": "ns/iter",
            "extra": "iterations: 3445189\ncpu: 194.89714265313165 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 230.33668442450343,
            "unit": "ns/iter",
            "extra": "iterations: 2783372\ncpu: 230.3363926920297 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2926.576308727086,
            "unit": "ns/iter",
            "extra": "iterations: 232516\ncpu: 2926.606220647183 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "2f9a76c788600ec91bde703109c9962c54a90136",
          "message": "Merge pull request #32 from benmandrew/feat/timing-strengthen\n\nStrengthen assumptions, weaken guarantees",
          "timestamp": "2026-07-21T17:04:49Z",
          "url": "https://github.com/benmandrew/counter/commit/2f9a76c788600ec91bde703109c9962c54a90136"
        },
        "date": 1784699265083,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 442.9120693454378,
            "unit": "ns/iter",
            "extra": "iterations: 1543000\ncpu: 442.9199915748542 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1747.3481414507787,
            "unit": "ns/iter",
            "extra": "iterations: 395954\ncpu: 1746.8494623112783 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 495.06573620315976,
            "unit": "ns/iter",
            "extra": "iterations: 1408402\ncpu: 495.062092357154 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 139.67873111322015,
            "unit": "ns/iter",
            "extra": "iterations: 5000257\ncpu: 139.67639363336718 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 157.81823244994416,
            "unit": "ns/iter",
            "extra": "iterations: 4429487\ncpu: 157.82246296241533 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 175.9321588586964,
            "unit": "ns/iter",
            "extra": "iterations: 3981979\ncpu: 175.9303572419643 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 219.34573426503624,
            "unit": "ns/iter",
            "extra": "iterations: 3212178\ncpu: 219.3453002915779 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2815.553878423627,
            "unit": "ns/iter",
            "extra": "iterations: 247910\ncpu: 2815.5432334314864 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "id": "6f0dfbe31ccd4e817ac3f1aa7f53b41c143fec55",
          "message": "fix: raise wellsep lift timeout cap to 600s\n\nThe 3-seed calibration put lift at ~27s, but the live run showed a heavy\ntail — hard seeds run 100-600s+ across all four arms, intrinsic to lift's\nrealizability cost, not the well-sep/output-assumption feature. At the\n180s cap those seeds censored (false implies_ideal=0, truncated\nwall_time) in every arm. Raised to the muc/padd 600s; the other four\nspecs keep their calibrated caps (all <20s, no censoring).",
          "timestamp": "2026-07-22T19:23:03Z",
          "url": "https://github.com/benmandrew/counter/commit/6f0dfbe31ccd4e817ac3f1aa7f53b41c143fec55"
        },
        "date": 1784785956164,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 431.63944993897917,
            "unit": "ns/iter",
            "extra": "iterations: 1626365\ncpu: 431.4723078767682 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1692.7598088289378,
            "unit": "ns/iter",
            "extra": "iterations: 415544\ncpu: 1692.1540270103765 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 434.29265281450756,
            "unit": "ns/iter",
            "extra": "iterations: 1594842\ncpu: 434.2087749131261 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 159.09577945476477,
            "unit": "ns/iter",
            "extra": "iterations: 4523444\ncpu: 159.05702358645323 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 190.18354115291046,
            "unit": "ns/iter",
            "extra": "iterations: 3971153\ncpu: 190.13071921429375 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 197.4906778074604,
            "unit": "ns/iter",
            "extra": "iterations: 3589928\ncpu: 197.4251269106233 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 248.49274761343887,
            "unit": "ns/iter",
            "extra": "iterations: 2971022\ncpu: 248.40528175153196 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2692.1633746959224,
            "unit": "ns/iter",
            "extra": "iterations: 258204\ncpu: 2690.814150826483 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "id": "776f750fd1c5977ad9fc4079806e20d09b5c3c2f",
          "message": "tune: calibrate arbiter-hp to pop10000/gen100, 600s cap",
          "timestamp": "2026-07-23T11:05:46Z",
          "url": "https://github.com/benmandrew/counter/commit/776f750fd1c5977ad9fc4079806e20d09b5c3c2f"
        },
        "date": 1784872117979,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 541.6384708490688,
            "unit": "ns/iter",
            "extra": "iterations: 1301245\ncpu: 541.5739964418692 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2246.6129362819274,
            "unit": "ns/iter",
            "extra": "iterations: 309347\ncpu: 2246.4842296838187 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 582.6669672150647,
            "unit": "ns/iter",
            "extra": "iterations: 1178956\ncpu: 582.6178619049394 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 196.9081397355067,
            "unit": "ns/iter",
            "extra": "iterations: 3558666\ncpu: 196.90232070107172 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 221.79977914268864,
            "unit": "ns/iter",
            "extra": "iterations: 3130528\ncpu: 221.77786335084696 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 251.88581527786997,
            "unit": "ns/iter",
            "extra": "iterations: 2826245\ncpu: 251.8549188056946 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 298.09514252537,
            "unit": "ns/iter",
            "extra": "iterations: 2313939\ncpu: 298.05879584552565 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3457.1815919046885,
            "unit": "ns/iter",
            "extra": "iterations: 202487\ncpu: 3456.877853886915 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "e8b24605abc103f13c7fa1c1234cac6553f743ad",
          "message": "Merge pull request #37 from benmandrew/refactor/readability\n\nrefactor: dedupe and split complex functions for readability",
          "timestamp": "2026-07-24T17:00:47Z",
          "url": "https://github.com/benmandrew/counter/commit/e8b24605abc103f13c7fa1c1234cac6553f743ad"
        },
        "date": 1784958129072,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 567.4695539338065,
            "unit": "ns/iter",
            "extra": "iterations: 1254612\ncpu: 567.4463252384005 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2312.7400005286454,
            "unit": "ns/iter",
            "extra": "iterations: 302666\ncpu: 2312.5250804517195 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 588.8066181400667,
            "unit": "ns/iter",
            "extra": "iterations: 1160326\ncpu: 588.7874666257586 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 196.72762346871826,
            "unit": "ns/iter",
            "extra": "iterations: 3558695\ncpu: 196.68408419378454 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 222.21285149764256,
            "unit": "ns/iter",
            "extra": "iterations: 3098456\ncpu: 222.19466308380677 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 248.5731592820951,
            "unit": "ns/iter",
            "extra": "iterations: 2823613\ncpu: 248.53997874354624 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 298.0583391552379,
            "unit": "ns/iter",
            "extra": "iterations: 2346023\ncpu: 298.0535932512167 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3547.3554058087398,
            "unit": "ns/iter",
            "extra": "iterations: 197667\ncpu: 3546.6080327014643 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "id": "eb362fb4b0f9dc8a60df518901cba1f69468bbd5",
          "message": "ci: gate the whole nightly run on new commits\n\nOnly the benchmark job checked whether commits had landed since the\nprevious nightly. The check job (full-tree lint plus docs) and the\nfour-configuration build-and-test matrix ran unconditionally, re-deriving\nthe same results from an unchanged tree and a flake-pinned toolchain.\nFive heavy runners for no signal.\n\nA cheap gate job now runs the 25h staleness check once and exports the\nverdict. check and build-matrix consume it; build-and-test inherits the\nskip, since a job whose dependency is skipped is itself skipped. The\nbenchmark job drops its inline check and the seven step conditions that\nhung off it.\n\nNon-schedule events are unaffected: the gate short-circuits to run=true\nfor pushes, pull requests, and manual dispatch, so the existing\nchanged-file lint scoping and matrix trimming still decide those runs.",
          "timestamp": "2026-07-27T16:01:54Z",
          "url": "https://github.com/benmandrew/counter/commit/eb362fb4b0f9dc8a60df518901cba1f69468bbd5"
        },
        "date": 1785217525958,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 533.1750032901185,
            "unit": "ns/iter",
            "extra": "iterations: 1314541\ncpu: 533.1170887785166 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2219.269629418225,
            "unit": "ns/iter",
            "extra": "iterations: 315585\ncpu: 2218.972099434384 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 600.5853074169632,
            "unit": "ns/iter",
            "extra": "iterations: 1162818\ncpu: 600.5486206783863 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 210.9394238116121,
            "unit": "ns/iter",
            "extra": "iterations: 3558593\ncpu: 210.91373669312557 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 224.19184791961487,
            "unit": "ns/iter",
            "extra": "iterations: 3155489\ncpu: 224.18146347523296 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 249.09445465713785,
            "unit": "ns/iter",
            "extra": "iterations: 2824911\ncpu: 249.058995132944 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 297.6175653261173,
            "unit": "ns/iter",
            "extra": "iterations: 2351280\ncpu: 297.6072713585793 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3519.867682607525,
            "unit": "ns/iter",
            "extra": "iterations: 198568\ncpu: 3519.127004351155 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "3d005329fd6ccf72b026081aff3aa257372dbd8b",
          "message": "Merge pull request #45 from benmandrew/feat/campaign-commit-provenance\n\nfeat: record the commit each campaign ran against",
          "timestamp": "2026-07-30T17:48:15Z",
          "url": "https://github.com/benmandrew/counter/commit/3d005329fd6ccf72b026081aff3aa257372dbd8b"
        },
        "date": 1785478877244,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 546.0212620726332,
            "unit": "ns/iter",
            "extra": "iterations: 1236474\ncpu: 545.95086835631 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2254.6817047442105,
            "unit": "ns/iter",
            "extra": "iterations: 310944\ncpu: 2254.4266523875676 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 584.0216287803537,
            "unit": "ns/iter",
            "extra": "iterations: 1052579\ncpu: 583.9496132831835 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 198.2360886530508,
            "unit": "ns/iter",
            "extra": "iterations: 3471285\ncpu: 198.20426326273991 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 224.10612028144598,
            "unit": "ns/iter",
            "extra": "iterations: 3095789\ncpu: 224.078832245996 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 250.42865809025596,
            "unit": "ns/iter",
            "extra": "iterations: 2792468\ncpu: 250.39179607429708 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 301.56303632311364,
            "unit": "ns/iter",
            "extra": "iterations: 2337327\ncpu: 301.4857831189219 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3445.739486333306,
            "unit": "ns/iter",
            "extra": "iterations: 202427\ncpu: 3445.020180114316 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "7bd8b0fbfdbbac1bd340fb7b70c12feef54bd662",
          "message": "Merge pull request #47 from benmandrew/fix/subprocess-containment\n\nfix(runner): contain and timeout every fork/exec, not just spot's",
          "timestamp": "2026-07-31T16:01:58Z",
          "url": "https://github.com/benmandrew/counter/commit/7bd8b0fbfdbbac1bd340fb7b70c12feef54bd662"
        },
        "date": 1785564507090,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 539.7771348356159,
            "unit": "ns/iter",
            "extra": "iterations: 1230973\ncpu: 539.7137045247946 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2330.6036630085396,
            "unit": "ns/iter",
            "extra": "iterations: 299972\ncpu: 2330.434547224407 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 581.3488201996449,
            "unit": "ns/iter",
            "extra": "iterations: 1174436\ncpu: 581.2386924447137 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 197.485115943963,
            "unit": "ns/iter",
            "extra": "iterations: 3536536\ncpu: 197.47592729156437 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 222.9042694115654,
            "unit": "ns/iter",
            "extra": "iterations: 3143384\ncpu: 222.8891284042929 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 249.13499919540035,
            "unit": "ns/iter",
            "extra": "iterations: 2808876\ncpu: 249.12114418721237 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 299.4385915300151,
            "unit": "ns/iter",
            "extra": "iterations: 2337430\ncpu: 299.40352951746144 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3507.070115546157,
            "unit": "ns/iter",
            "extra": "iterations: 198016\ncpu: 3506.924319246929 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "e56cdfd02d432c495456c480463a4002986826f3",
          "message": "Merge pull request #63 from benmandrew/refactor/unify-dispatch-window\n\nrefactor: one pool-derived dispatch window; parallel TLSF final re-check",
          "timestamp": "2026-08-04T21:22:35Z",
          "url": "https://github.com/benmandrew/counter/commit/e56cdfd02d432c495456c480463a4002986826f3"
        },
        "date": 1785909210426,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 528.6214670557101,
            "unit": "ns/iter",
            "extra": "iterations: 1328743\ncpu: 528.591698319389 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2204.7785789342556,
            "unit": "ns/iter",
            "extra": "iterations: 316903\ncpu: 2204.576207861712 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 629.1311872433844,
            "unit": "ns/iter",
            "extra": "iterations: 1114026\ncpu: 629.0774129149589 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 199.13272445987818,
            "unit": "ns/iter",
            "extra": "iterations: 3516330\ncpu: 199.1040323291613 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 224.9348325819118,
            "unit": "ns/iter",
            "extra": "iterations: 3112537\ncpu: 224.91667408291053 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 250.92503454108126,
            "unit": "ns/iter",
            "extra": "iterations: 2783642\ncpu: 250.90804923909053 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 303.687786999776,
            "unit": "ns/iter",
            "extra": "iterations: 2315115\ncpu: 303.65992747660493 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3440.0803647565535,
            "unit": "ns/iter",
            "extra": "iterations: 202875\ncpu: 3439.5369611829947 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "id": "4f481cc13ddc78fa6cd9ffa8997237c29225d616",
          "message": "fix(process_tests): keep the RSS ballast resident under -O2\n\nThe ballast test added in 24b6f49 guarded its 512MB buffer with\n`ballast.front() == '\\1' && ballast.back() == '\\1'`, which a\nrelease build folds to a constant, letting it elide the whole\nallocation. The test passed vacuously in debug and failed CI's\nrelease leg (\"the floor should reflect the ballast this process\nis holding, got 4768kB\").\n\nRoute every access through a new `touch_every_page` helper that\nwrites one byte per page through a `volatile` view and reads it\nback into a checksum: volatile accesses are observable, so both\nthe buffer and the stores into it survive optimisation. Measured\nat -O2 on both compilers, the old guard left the process resident\nat ~3.5MB; the new helper leaves it at ~515MB, above the 256MB\nfloor the assertion checks.",
          "timestamp": "2026-08-05T16:30:56Z",
          "url": "https://github.com/benmandrew/counter/commit/4f481cc13ddc78fa6cd9ffa8997237c29225d616"
        },
        "date": 1785995849902,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 540.5362496452442,
            "unit": "ns/iter",
            "extra": "iterations: 1229681\ncpu: 540.425067151562 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2383.0540493266185,
            "unit": "ns/iter",
            "extra": "iterations: 280133\ncpu: 2382.0115802136843 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 613.3723088590467,
            "unit": "ns/iter",
            "extra": "iterations: 1128657\ncpu: 613.2909245235708 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 198.87620712446267,
            "unit": "ns/iter",
            "extra": "iterations: 3519314\ncpu: 198.85133977815 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 224.88403209180962,
            "unit": "ns/iter",
            "extra": "iterations: 3109205\ncpu: 224.8550571609142 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 250.87054652657397,
            "unit": "ns/iter",
            "extra": "iterations: 2788245\ncpu: 250.8613328455711 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 302.35353673088065,
            "unit": "ns/iter",
            "extra": "iterations: 2317465\ncpu: 302.31606043672735 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3523.9486736044773,
            "unit": "ns/iter",
            "extra": "iterations: 197754\ncpu: 3522.621509552272 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "id": "55b72aa61a8b8464605f5dcc3ba7d23288b6873c",
          "message": "docs(readme): merge duplicated commands section, drop stale defaults\n\nThe Commands section listed each binary twice — a `console` synopsis\nblock and a purpose table — and repeated flags `--help` already\ndocuments. Fold them into one table carrying invocation and purpose,\nwith a single line explaining `<spec>` instead of repeating\n`<spec.json|spec.tlsf>` five times.\n\nDrop the inline `population_size` (200) and `generations` (10)\ndefaults from \"How it works\": they duplicate the configuration docs\npage and go stale in the README whenever a default changes there. Drop\nthe fitness-order sentence from the quickstart, which the linked TLSF\nguide already covers. Fix \"unrealizable\" to \"unrealisable\" in the\n`mucs` row to match the spelling used everywhere else in the file.",
          "timestamp": "2026-08-07T00:01:23Z",
          "url": "https://github.com/benmandrew/counter/commit/55b72aa61a8b8464605f5dcc3ba7d23288b6873c"
        },
        "date": 1786078913077,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 542.1697811290234,
            "unit": "ns/iter",
            "extra": "iterations: 1298619\ncpu: 542.1362847763663 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2222.7009921313233,
            "unit": "ns/iter",
            "extra": "iterations: 312761\ncpu: 2222.6121767100117 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 632.7769645325858,
            "unit": "ns/iter",
            "extra": "iterations: 1103125\ncpu: 632.7630594900849 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 197.0089012761393,
            "unit": "ns/iter",
            "extra": "iterations: 3545222\ncpu: 197.00449224336307 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 222.446454069028,
            "unit": "ns/iter",
            "extra": "iterations: 3126344\ncpu: 222.43410545992379 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 249.423644484751,
            "unit": "ns/iter",
            "extra": "iterations: 2818467\ncpu: 249.41116394124896 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 298.33262507770144,
            "unit": "ns/iter",
            "extra": "iterations: 2347081\ncpu: 298.31996211464383 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3451.2950208986217,
            "unit": "ns/iter",
            "extra": "iterations: 202406\ncpu: 3451.1297935831935 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "6adcdc28d28a72b7d11a871f6310e175955cf7a4",
          "message": "Merge pull request #92 from benmandrew/feat/vacuity-filter\n\nFold false-condition into the vacuity filter and add a tautology screen",
          "timestamp": "2026-08-07T17:45:43Z",
          "url": "https://github.com/benmandrew/counter/commit/6adcdc28d28a72b7d11a871f6310e175955cf7a4"
        },
        "date": 1786162634043,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 568.9960538151162,
            "unit": "ns/iter",
            "extra": "iterations: 1251842\ncpu: 568.8845213693102 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2215.3343851669533,
            "unit": "ns/iter",
            "extra": "iterations: 316590\ncpu: 2214.148128494268 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 581.7711783811742,
            "unit": "ns/iter",
            "extra": "iterations: 1185041\ncpu: 581.7277866335426 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 201.88490148270694,
            "unit": "ns/iter",
            "extra": "iterations: 3258565\ncpu: 201.8755301796956 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 251.81458062838757,
            "unit": "ns/iter",
            "extra": "iterations: 3038415\ncpu: 251.79367202966006 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 272.7950621151053,
            "unit": "ns/iter",
            "extra": "iterations: 2560327\ncpu: 272.78807941329376 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 353.2449528290823,
            "unit": "ns/iter",
            "extra": "iterations: 1979822\ncpu: 353.22356908853413 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3297.8636599372894,
            "unit": "ns/iter",
            "extra": "iterations: 212102\ncpu: 3297.7491914267684 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "51f24c85bd8b2da69b0523d4a9c29717a7324fba",
          "message": "Merge pull request #97 from benmandrew/docs/elitism-campaign-defaults\n\nArchive the elitism campaign and retire the lily02 anecdote",
          "timestamp": "2026-08-10T17:36:33Z",
          "url": "https://github.com/benmandrew/counter/commit/51f24c85bd8b2da69b0523d4a9c29717a7324fba"
        },
        "date": 1786422431131,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 438.22613304032336,
            "unit": "ns/iter",
            "extra": "iterations: 1587278\ncpu: 438.2079024594305 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1741.2790910887998,
            "unit": "ns/iter",
            "extra": "iterations: 404660\ncpu: 1741.2021400682052 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 501.59044400004404,
            "unit": "ns/iter",
            "extra": "iterations: 1000000\ncpu: 501.58432299999987 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 150.35866282425906,
            "unit": "ns/iter",
            "extra": "iterations: 4654377\ncpu: 150.35270133897617 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 169.9776659847524,
            "unit": "ns/iter",
            "extra": "iterations: 4120710\ncpu: 169.96959092001137 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 192.14990365449154,
            "unit": "ns/iter",
            "extra": "iterations: 3553876\ncpu: 192.0862452713602 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 238.06191399250235,
            "unit": "ns/iter",
            "extra": "iterations: 2904933\ncpu: 237.99118327341824 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2853.9209637885583,
            "unit": "ns/iter",
            "extra": "iterations: 244369\ncpu: 2853.2088194492762 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "f7a140c46eb1cff0ba3700217c5cb1f795440a10",
          "message": "Merge pull request #104 from benmandrew/feat/campaign-backport\n\nfeat(scripts): derive campaign declarations for pre-declaration archives",
          "timestamp": "2026-08-11T17:17:53Z",
          "url": "https://github.com/benmandrew/counter/commit/f7a140c46eb1cff0ba3700217c5cb1f795440a10"
        },
        "date": 1786510521281,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 477.0206672944504,
            "unit": "ns/iter",
            "extra": "iterations: 1456988\ncpu: 476.8849530675613 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1809.9824371698435,
            "unit": "ns/iter",
            "extra": "iterations: 378242\ncpu: 1809.953357903142 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 522.1957130168978,
            "unit": "ns/iter",
            "extra": "iterations: 1355592\ncpu: 522.1628152128368 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 146.76965518663962,
            "unit": "ns/iter",
            "extra": "iterations: 4751672\ncpu: 146.7668948951022 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 165.4615837644908,
            "unit": "ns/iter",
            "extra": "iterations: 3981741\ncpu: 165.4531974832114 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 199.67092800262222,
            "unit": "ns/iter",
            "extra": "iterations: 3831584\ncpu: 199.66785799293461 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 238.22140023141404,
            "unit": "ns/iter",
            "extra": "iterations: 2860670\ncpu: 238.21193846196857 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2921.9351799896012,
            "unit": "ns/iter",
            "extra": "iterations: 244153\ncpu: 2921.9055674106016 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "645bb99de36003df58bc7a14eb3b6845a240699a",
          "message": "Merge pull request #101 from benmandrew/campaign/selection-default\n\nfeat(experiments): the selection-default A/B, plus per-spec TLSF caps and --pin-vintage",
          "timestamp": "2026-08-12T13:18:52Z",
          "url": "https://github.com/benmandrew/counter/commit/645bb99de36003df58bc7a14eb3b6845a240699a"
        },
        "date": 1786597186246,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 450.9793629427409,
            "unit": "ns/iter",
            "extra": "iterations: 1605413\ncpu: 450.9642671387363 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1959.2906606436488,
            "unit": "ns/iter",
            "extra": "iterations: 363933\ncpu: 1959.2598143064793 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 517.4383566641644,
            "unit": "ns/iter",
            "extra": "iterations: 1315641\ncpu: 517.3809800697911 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 142.49598530366802,
            "unit": "ns/iter",
            "extra": "iterations: 4877953\ncpu: 142.48868449532006 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 163.31760429738688,
            "unit": "ns/iter",
            "extra": "iterations: 4324677\ncpu: 163.2898207195589 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 180.31673301515883,
            "unit": "ns/iter",
            "extra": "iterations: 3713724\ncpu: 180.29140291524084 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 225.22847826230142,
            "unit": "ns/iter",
            "extra": "iterations: 3032980\ncpu: 225.18998740512632 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2938.965336892627,
            "unit": "ns/iter",
            "extra": "iterations: 247987\ncpu: 2938.578110949365 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "e11b2f35a531750e2184d125646538a7d9bf0d58",
          "message": "Merge pull request #116 from benmandrew/feat/spot-satisfiability-routing\n\nAsk SPOT first for satisfiability, escalate to black by query polarity",
          "timestamp": "2026-08-13T18:12:22Z",
          "url": "https://github.com/benmandrew/counter/commit/e11b2f35a531750e2184d125646538a7d9bf0d58"
        },
        "date": 1786683406195,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 599.5393131643374,
            "unit": "ns/iter",
            "extra": "iterations: 1255788\ncpu: 599.282934699169 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2336.860649210959,
            "unit": "ns/iter",
            "extra": "iterations: 300149\ncpu: 2336.3083868345384 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 642.1152450585008,
            "unit": "ns/iter",
            "extra": "iterations: 1082823\ncpu: 641.9422130856103 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 198.18221540132276,
            "unit": "ns/iter",
            "extra": "iterations: 3493832\ncpu: 198.11344821388093 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 227.5785239727305,
            "unit": "ns/iter",
            "extra": "iterations: 3102734\ncpu: 227.4829788824954 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 251.87132384879817,
            "unit": "ns/iter",
            "extra": "iterations: 2543999\ncpu: 251.86498422365722 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 305.21776502534897,
            "unit": "ns/iter",
            "extra": "iterations: 2273726\ncpu: 305.1031175260343 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3338.2640379752834,
            "unit": "ns/iter",
            "extra": "iterations: 209610\ncpu: 3337.1423834740704 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "e35816bc0e74b1539a8b9443dcceb193bc02f6df",
          "message": "Merge pull request #124 from benmandrew/feat/aurus-crossover\n\nfeat(genetic)!: graft crossover donors from anywhere in the parent",
          "timestamp": "2026-08-14T16:13:07Z",
          "url": "https://github.com/benmandrew/counter/commit/e35816bc0e74b1539a8b9443dcceb193bc02f6df"
        },
        "date": 1786765704282,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 531.5709979295752,
            "unit": "ns/iter",
            "extra": "iterations: 1283798\ncpu: 531.4071528386864 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2105.1120571552397,
            "unit": "ns/iter",
            "extra": "iterations: 332848\ncpu: 2104.322955222804 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 607.4378317048256,
            "unit": "ns/iter",
            "extra": "iterations: 1151061\ncpu: 607.2991109941175 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 175.33905320523178,
            "unit": "ns/iter",
            "extra": "iterations: 3932489\ncpu: 175.34141049091298 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 198.1355952965621,
            "unit": "ns/iter",
            "extra": "iterations: 3526612\ncpu: 198.12745689063615 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 219.62847189709933,
            "unit": "ns/iter",
            "extra": "iterations: 3193247\ncpu: 219.63180737349802 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 269.3588153931054,
            "unit": "ns/iter",
            "extra": "iterations: 2620785\ncpu: 269.34739438755935 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3284.4291209177527,
            "unit": "ns/iter",
            "extra": "iterations: 212665\ncpu: 3284.522690616695 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "882b4d93a0159fe5f1bc700534aad352caadeb91",
          "message": "Merge pull request #129 from benmandrew/feat/tick-auto-stage\n\nfeat(experiments): auto-stage the tick's checkout to its entry's branch",
          "timestamp": "2026-08-19T17:22:06Z",
          "url": "https://github.com/benmandrew/counter/commit/882b4d93a0159fe5f1bc700534aad352caadeb91"
        },
        "date": 1787198063932,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 569.1511613284898,
            "unit": "ns/iter",
            "extra": "iterations: 1288266\ncpu: 569.1171955170748 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2292.759803276667,
            "unit": "ns/iter",
            "extra": "iterations: 296457\ncpu: 2292.7222261575876 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 627.8266009660792,
            "unit": "ns/iter",
            "extra": "iterations: 1082284\ncpu: 627.8077916702085 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 199.91684192188484,
            "unit": "ns/iter",
            "extra": "iterations: 3501560\ncpu: 199.9043223591771 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 223.81659312093458,
            "unit": "ns/iter",
            "extra": "iterations: 3125946\ncpu: 223.7925808699191 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 249.47523194463005,
            "unit": "ns/iter",
            "extra": "iterations: 2808860\ncpu: 249.40820582015493 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 300.78146300104027,
            "unit": "ns/iter",
            "extra": "iterations: 2328187\ncpu: 300.7182447114429 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3350.1461573507663,
            "unit": "ns/iter",
            "extra": "iterations: 208515\ncpu: 3350.1020981703978 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "0376702d9edc0b3e918073611bc51a7bd069b7c1",
          "message": "Merge pull request #136 from benmandrew/fix/tick-stage-configs\n\nCheck a tick's configs before it runs a phase",
          "timestamp": "2026-08-20T23:34:43Z",
          "url": "https://github.com/benmandrew/counter/commit/0376702d9edc0b3e918073611bc51a7bd069b7c1"
        },
        "date": 1787284622299,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 9.23501808778815,
            "unit": "ns/iter",
            "extra": "iterations: 75552355\ncpu: 9.225755054226967 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 131.74110505473737,
            "unit": "ns/iter",
            "extra": "iterations: 5366250\ncpu: 131.67869778709522 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 122.28888258142062,
            "unit": "ns/iter",
            "extra": "iterations: 5915393\ncpu: 122.23886832202021 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 79.55838883262857,
            "unit": "ns/iter",
            "extra": "iterations: 9033748\ncpu: 79.55460413551499 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 561.6233621625382,
            "unit": "ns/iter",
            "extra": "iterations: 1246537\ncpu: 561.4296823920988 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2254.41566176134,
            "unit": "ns/iter",
            "extra": "iterations: 311638\ncpu: 2253.659986907886 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 494.19077059695525,
            "unit": "ns/iter",
            "extra": "iterations: 1427893\ncpu: 494.15749849603594 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 200.82732238867152,
            "unit": "ns/iter",
            "extra": "iterations: 3478100\ncpu: 200.82222765302882 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 236.54876140198462,
            "unit": "ns/iter",
            "extra": "iterations: 3037628\ncpu: 236.52566410370233 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 252.81020687846927,
            "unit": "ns/iter",
            "extra": "iterations: 2761138\ncpu: 252.7835211423699 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 309.2275489481229,
            "unit": "ns/iter",
            "extra": "iterations: 2295594\ncpu: 309.19805244307116 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3109.8746875056145,
            "unit": "ns/iter",
            "extra": "iterations: 225604\ncpu: 3109.788780340779 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "id": "adfb3803fc77d1b8ce4d068603d1d1c3a93c2d5c",
          "message": "docs(experiments): compare sweep-O arms against the AuRUS reference\n\nBoth sweep-O TLSF campaigns had never been read against the 2026-08-14\nhead-to-head, though PLAN §4 and §10 committed to using its archived\nAuRUS rows as the reference rather than re-running AuRUS. Restricting\nto the 10 families the two corpora share drops the cluster count to 7,\nwhere `analyse_aurus_h2h.py` computes the comparison unchanged -- it\nscores over the corpus intersection on its own -- but the restricted\nverdict is illegible without the power floor at that unit count, which\nthe script only printed below 6, where it decides outcome 3 by\nconstruction. `power_floor()` replaces both inlined blocks and prints\nthe bound at every non-tied count instead.\n\nThe comparison itself: the archived arm restricted to the same 10\nfamilies reads p = 0.1562 against its own p = 0.0127 over 25, so\nrestriction dissolves the archived verdict before any arm change does.\nMean `implies_ideal` runs 0.265 archived to 0.400/0.395 on the two\nscreen-off sweep-O arms against AuRUS's 0.487, with every arm null.\nFour confounds separate the sweep-O arms from the archived one --\nselection scheme, model-counting metric, `run_well_separation`, and a\n600s wall cap against AuRUS's 7200s -- none of them a choice either\ncampaign made, all four `gen_configs` baselines that do not track the\nbinary default.\n\nTwo corrections fall out of reading the configs: `ops-grammar/PLAN.md`\n§4's reason for discarding the archived counter rows as a control does\nnot hold, both campaigns having run a 10000ms `ltlsynt` budget; and\n`--pin-vintage` reads `gen_configs.DEFAULTS`, whose `run_well_separation`\nentry has been stale since `b101ada`, so the head-to-head ran the\nper-generation filter the shipping binary does not.",
          "timestamp": "2026-08-21T17:39:22Z",
          "url": "https://github.com/benmandrew/counter/commit/adfb3803fc77d1b8ce4d068603d1d1c3a93c2d5c"
        },
        "date": 1787370587616,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 9.386076489359343,
            "unit": "ns/iter",
            "extra": "iterations: 74813517\ncpu: 9.384842006558788 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 123.13389249355079,
            "unit": "ns/iter",
            "extra": "iterations: 5611868\ncpu: 123.12148984972559 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 108.72303520212945,
            "unit": "ns/iter",
            "extra": "iterations: 6452394\ncpu: 108.71593287700657 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 74.6755303426177,
            "unit": "ns/iter",
            "extra": "iterations: 9093744\ncpu: 74.66589943591987 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 543.2756561017125,
            "unit": "ns/iter",
            "extra": "iterations: 1290463\ncpu: 543.2366220496054 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2244.912711503388,
            "unit": "ns/iter",
            "extra": "iterations: 312229\ncpu: 2244.211697183798 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 515.8974267103948,
            "unit": "ns/iter",
            "extra": "iterations: 1367588\ncpu: 515.7023686958358 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 198.0948615144635,
            "unit": "ns/iter",
            "extra": "iterations: 3447183\ncpu: 198.03101372918118 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 226.11790472008292,
            "unit": "ns/iter",
            "extra": "iterations: 3079054\ncpu: 226.02423374192196 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 252.22973989284833,
            "unit": "ns/iter",
            "extra": "iterations: 2745753\ncpu: 252.164246019216 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 300.54049844838545,
            "unit": "ns/iter",
            "extra": "iterations: 2330151\ncpu: 300.46884901450636 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3144.5688004799717,
            "unit": "ns/iter",
            "extra": "iterations: 223356\ncpu: 3144.147065670946 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "97da5b96cb6fe21c1be1dc22b6ab1a542d5e41af",
          "message": "Merge pull request #142 from benmandrew/campaign/aurus-h2h-ship\n\nClose aurus-h2h-ship: counter and AuRUS no longer separate over the 25-family corpus",
          "timestamp": "2026-08-22T21:18:34Z",
          "url": "https://github.com/benmandrew/counter/commit/97da5b96cb6fe21c1be1dc22b6ab1a542d5e41af"
        },
        "date": 1787457450094,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 7.212896631966565,
            "unit": "ns/iter",
            "extra": "iterations: 98714812\ncpu: 7.212609197898285 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 108.50034834190406,
            "unit": "ns/iter",
            "extra": "iterations: 6477831\ncpu: 108.4869901669247 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 94.07222725519695,
            "unit": "ns/iter",
            "extra": "iterations: 7568971\ncpu: 94.06089163242927 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 56.71645326658784,
            "unit": "ns/iter",
            "extra": "iterations: 12474769\ncpu: 56.71610263885449 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 452.4260983505051,
            "unit": "ns/iter",
            "extra": "iterations: 1563754\ncpu: 452.38065833884394 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1785.0904548192013,
            "unit": "ns/iter",
            "extra": "iterations: 396839\ncpu: 1784.9903462109312 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 389.1875991015923,
            "unit": "ns/iter",
            "extra": "iterations: 1774694\ncpu: 389.0228659137855 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 166.20105838593392,
            "unit": "ns/iter",
            "extra": "iterations: 4235695\ncpu: 166.10805499451695 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 188.8859790646564,
            "unit": "ns/iter",
            "extra": "iterations: 3794540\ncpu: 188.79127878478022 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 212.66587218517364,
            "unit": "ns/iter",
            "extra": "iterations: 3343900\ncpu: 212.60172911869356 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 253.69715884701753,
            "unit": "ns/iter",
            "extra": "iterations: 2783905\ncpu: 253.61356691410097 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2500.264417392573,
            "unit": "ns/iter",
            "extra": "iterations: 271495\ncpu: 2499.568592423438 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "601ad58bce599f146be05fa3c8b10aa1d17b7516",
          "message": "Merge pull request #144 from benmandrew/feat/monotone-operators\n\nLand the TLSF monotone operators, and set the defaults to the campaign's monoon arm",
          "timestamp": "2026-08-25T12:20:23Z",
          "url": "https://github.com/benmandrew/counter/commit/601ad58bce599f146be05fa3c8b10aa1d17b7516"
        },
        "date": 1787716798544,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 9.36802734191173,
            "unit": "ns/iter",
            "extra": "iterations: 74648182\ncpu: 9.365428484246275 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 127.83685900483454,
            "unit": "ns/iter",
            "extra": "iterations: 5384128\ncpu: 127.80280390807943 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 108.44012032822131,
            "unit": "ns/iter",
            "extra": "iterations: 6396837\ncpu: 108.4348020748379 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 77.36263355996279,
            "unit": "ns/iter",
            "extra": "iterations: 9024879\ncpu: 77.33935745842136 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 641.0799965136956,
            "unit": "ns/iter",
            "extra": "iterations: 1089985\ncpu: 640.8417097482998 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2278.3750348910216,
            "unit": "ns/iter",
            "extra": "iterations: 308102\ncpu: 2277.5095682598626 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 510.9401683302173,
            "unit": "ns/iter",
            "extra": "iterations: 1368857\ncpu: 510.91744937564744 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 198.794717783498,
            "unit": "ns/iter",
            "extra": "iterations: 3520719\ncpu: 198.7607642075383 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 225.9692641147732,
            "unit": "ns/iter",
            "extra": "iterations: 3101326\ncpu: 225.90413036230333 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 252.10871119088543,
            "unit": "ns/iter",
            "extra": "iterations: 2791065\ncpu: 252.01871257029092 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 302.8205658822749,
            "unit": "ns/iter",
            "extra": "iterations: 2296308\ncpu: 302.8002310665643 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3171.7480035330664,
            "unit": "ns/iter",
            "extra": "iterations: 220765\ncpu: 3171.619862749988 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "c466bf1ad47fb4f8f0ac3f7282a5f6b47e1176ed",
          "message": "Merge pull request #147 from benmandrew/campaign/selection-smoke\n\nMeasure the weighted selection scheme against the shipped nsga2-apportion",
          "timestamp": "2026-08-26T23:33:03Z",
          "url": "https://github.com/benmandrew/counter/commit/c466bf1ad47fb4f8f0ac3f7282a5f6b47e1176ed"
        },
        "date": 1787839471166,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 9.378369745714455,
            "unit": "ns/iter",
            "extra": "iterations: 74612900\ncpu: 9.378049667014686 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 127.35938252631274,
            "unit": "ns/iter",
            "extra": "iterations: 5489076\ncpu: 127.34457456956325 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 109.08592262809506,
            "unit": "ns/iter",
            "extra": "iterations: 6423081\ncpu: 109.05157836247126 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 75.16830509696489,
            "unit": "ns/iter",
            "extra": "iterations: 9319896\ncpu: 75.15898170966713 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 542.043715868148,
            "unit": "ns/iter",
            "extra": "iterations: 1291865\ncpu: 541.9567214840564 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2236.031135747494,
            "unit": "ns/iter",
            "extra": "iterations: 313177\ncpu: 2235.8393687914504 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 492.00568985185447,
            "unit": "ns/iter",
            "extra": "iterations: 1435011\ncpu: 491.9305538424445 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 197.7746562899969,
            "unit": "ns/iter",
            "extra": "iterations: 3540121\ncpu: 197.72501420149192 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 223.1875265537604,
            "unit": "ns/iter",
            "extra": "iterations: 3106904\ncpu: 223.12974716952962 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 249.17228194181055,
            "unit": "ns/iter",
            "extra": "iterations: 2814822\ncpu: 249.13515739183495 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 304.2134333881096,
            "unit": "ns/iter",
            "extra": "iterations: 2340318\ncpu: 304.17470104490025 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3174.716957809261,
            "unit": "ns/iter",
            "extra": "iterations: 220854\ncpu: 3174.54763780597 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "9258ef478495ef2f87fd8871ea9f565fb654702a",
          "message": "Merge pull request #155 from benmandrew/ci/docker-publish\n\nci: build and publish the image for amd64 and arm64",
          "timestamp": "2026-08-27T17:58:57Z",
          "url": "https://github.com/benmandrew/counter/commit/9258ef478495ef2f87fd8871ea9f565fb654702a"
        },
        "date": 1787930541224,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 5.155456162427384,
            "unit": "ns/iter",
            "extra": "iterations: 135303340\ncpu: 5.155209331861283 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 81.59448068593065,
            "unit": "ns/iter",
            "extra": "iterations: 8385571\ncpu: 81.56202946704522 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 67.2885646602882,
            "unit": "ns/iter",
            "extra": "iterations: 10382044\ncpu: 67.25726292433357 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 45.70028763141903,
            "unit": "ns/iter",
            "extra": "iterations: 15409304\ncpu: 45.67912872638504 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 360.0346629275907,
            "unit": "ns/iter",
            "extra": "iterations: 1944152\ncpu: 359.87987564758294 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1438.929072551679,
            "unit": "ns/iter",
            "extra": "iterations: 477784\ncpu: 1438.4886329387334 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 327.0284119733767,
            "unit": "ns/iter",
            "extra": "iterations: 2148566\ncpu: 326.9966270526481 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 125.68415322998108,
            "unit": "ns/iter",
            "extra": "iterations: 5568466\ncpu: 125.66322682045646 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 141.59198197418354,
            "unit": "ns/iter",
            "extra": "iterations: 4951568\ncpu: 141.57060914845567 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 157.20974346712816,
            "unit": "ns/iter",
            "extra": "iterations: 4445707\ncpu: 157.19432319763754 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 191.03777774321338,
            "unit": "ns/iter",
            "extra": "iterations: 3667371\ncpu: 191.02154677015253 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2028.7710688800175,
            "unit": "ns/iter",
            "extra": "iterations: 345296\ncpu: 2028.3108550345214 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "cf08bc9aa0012a630f701aa881957b1c36173734",
          "message": "Merge pull request #161 from benmandrew/feat/termination-modes\n\nfeat(genetic): give the search a per-run termination budget",
          "timestamp": "2026-08-28T17:46:33Z",
          "url": "https://github.com/benmandrew/counter/commit/cf08bc9aa0012a630f701aa881957b1c36173734"
        },
        "date": 1787997656593,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 9.401789620471552,
            "unit": "ns/iter",
            "extra": "iterations: 74780660\ncpu: 9.400071863500536 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 126.37584831238384,
            "unit": "ns/iter",
            "extra": "iterations: 5416342\ncpu: 126.36404680502078 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 111.37062441897754,
            "unit": "ns/iter",
            "extra": "iterations: 6242171\ncpu: 111.36785695233277 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 73.73510300130694,
            "unit": "ns/iter",
            "extra": "iterations: 9137797\ncpu: 73.73295631321203 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 569.9264646992956,
            "unit": "ns/iter",
            "extra": "iterations: 1231123\ncpu: 569.89861208019 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2228.856179315643,
            "unit": "ns/iter",
            "extra": "iterations: 312834\ncpu: 2228.712157246334 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 612.9807499849436,
            "unit": "ns/iter",
            "extra": "iterations: 1127116\ncpu: 612.9486468118636 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 197.22540438447584,
            "unit": "ns/iter",
            "extra": "iterations: 3523565\ncpu: 197.20841732733774 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 223.45300934848675,
            "unit": "ns/iter",
            "extra": "iterations: 3149818\ncpu: 223.40148732402974 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 249.6115269378915,
            "unit": "ns/iter",
            "extra": "iterations: 2822623\ncpu: 249.5466323345342 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 299.8449463964668,
            "unit": "ns/iter",
            "extra": "iterations: 2330723\ncpu: 299.7406757473969 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3344.14510904668,
            "unit": "ns/iter",
            "extra": "iterations: 210552\ncpu: 3343.3799631444967 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "a10ecbcadba057ca4346f1b2ca81603c3461284b",
          "message": "Merge pull request #166 from benmandrew/feat/weaken-always\n\nfeat(genetic): let Always weaken by borrowing a donor's tick count",
          "timestamp": "2026-09-02T14:23:47Z",
          "url": "https://github.com/benmandrew/counter/commit/a10ecbcadba057ca4346f1b2ca81603c3461284b"
        },
        "date": 1788422538997,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 9.003569503692937,
            "unit": "ns/iter",
            "extra": "iterations: 79003140\ncpu: 9.000519004687662 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 148.33527939171816,
            "unit": "ns/iter",
            "extra": "iterations: 4822226\ncpu: 148.13493374221784 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 130.0369863818747,
            "unit": "ns/iter",
            "extra": "iterations: 5360135\ncpu: 129.9848893731221 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 81.77647631232652,
            "unit": "ns/iter",
            "extra": "iterations: 8683630\ncpu: 81.74549986583949 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 555.9517845126437,
            "unit": "ns/iter",
            "extra": "iterations: 1252751\ncpu: 555.757198357854 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2178.930074609982,
            "unit": "ns/iter",
            "extra": "iterations: 321271\ncpu: 2178.2874115622026 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 575.1842878095833,
            "unit": "ns/iter",
            "extra": "iterations: 1194512\ncpu: 575.1495129391748 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 200.6026641810324,
            "unit": "ns/iter",
            "extra": "iterations: 3415909\ncpu: 200.57415815233946 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 225.97110424758304,
            "unit": "ns/iter",
            "extra": "iterations: 3016914\ncpu: 225.95333012475677 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 253.4431499087951,
            "unit": "ns/iter",
            "extra": "iterations: 2756029\ncpu: 253.39275203562795 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 305.0518077801634,
            "unit": "ns/iter",
            "extra": "iterations: 2301469\ncpu: 305.0329837160525 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3533.3765174830673,
            "unit": "ns/iter",
            "extra": "iterations: 198190\ncpu: 3533.064559261313 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "cd71a96d0be503a614e3021a49c41ab58573449f",
          "message": "Merge pull request #170 from benmandrew/perf/final-filter-simplify\n\nperf(tlsf): run the final filters without the simplify pass",
          "timestamp": "2026-09-03T16:43:23Z",
          "url": "https://github.com/benmandrew/counter/commit/cd71a96d0be503a614e3021a49c41ab58573449f"
        },
        "date": 1788508683682,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 7.0317817571572885,
            "unit": "ns/iter",
            "extra": "iterations: 99524107\ncpu: 7.029137201904258 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 152.32201592468348,
            "unit": "ns/iter",
            "extra": "iterations: 4604676\ncpu: 152.26397210140297 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 106.87951237049955,
            "unit": "ns/iter",
            "extra": "iterations: 6577945\ncpu: 106.83627363865162 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 76.204251328526,
            "unit": "ns/iter",
            "extra": "iterations: 9178966\ncpu: 76.17358698136589 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 520.4435368456511,
            "unit": "ns/iter",
            "extra": "iterations: 1364651\ncpu: 520.4136808605274 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2058.1175320844677,
            "unit": "ns/iter",
            "extra": "iterations: 340741\ncpu: 2057.9951693515036 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 573.6148039751844,
            "unit": "ns/iter",
            "extra": "iterations: 1204771\ncpu: 573.5667193184435 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 167.37901346287887,
            "unit": "ns/iter",
            "extra": "iterations: 4180461\ncpu: 167.33573402550584 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 191.76367005763368,
            "unit": "ns/iter",
            "extra": "iterations: 3710939\ncpu: 191.6971092761157 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 210.8281040553028,
            "unit": "ns/iter",
            "extra": "iterations: 3327961\ncpu: 210.6868611140574 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 262.8668178008557,
            "unit": "ns/iter",
            "extra": "iterations: 2655918\ncpu: 262.7945271653721 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3458.2581604432758,
            "unit": "ns/iter",
            "extra": "iterations: 201766\ncpu: 3457.436827810434 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "371712a95df9a11a207c6cdf171a79a2e9686b47",
          "message": "Merge pull request #172 from benmandrew/campaign/selection-grading\n\ndocs(experiments): close the 2026-08-28-selection-grading archive",
          "timestamp": "2026-09-04T13:52:50Z",
          "url": "https://github.com/benmandrew/counter/commit/371712a95df9a11a207c6cdf171a79a2e9686b47"
        },
        "date": 1788593743734,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 7.5658112818499434,
            "unit": "ns/iter",
            "extra": "iterations: 93135080\ncpu: 7.564798365986266 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 126.68685721172146,
            "unit": "ns/iter",
            "extra": "iterations: 5310555\ncpu: 126.6561048703949 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 123.36089798013423,
            "unit": "ns/iter",
            "extra": "iterations: 5310897\ncpu: 123.3491790558167 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 66.50893549767387,
            "unit": "ns/iter",
            "extra": "iterations: 10556379\ncpu: 66.48693259307947 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 499.5088235799157,
            "unit": "ns/iter",
            "extra": "iterations: 1397732\ncpu: 499.473248805923 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2037.1821249709092,
            "unit": "ns/iter",
            "extra": "iterations: 343440\ncpu: 2036.9973037502912 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 556.3924619482626,
            "unit": "ns/iter",
            "extra": "iterations: 1249023\ncpu: 556.3485612354618 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 178.14348949733045,
            "unit": "ns/iter",
            "extra": "iterations: 3924336\ncpu: 178.14052033261166 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 201.03904432281593,
            "unit": "ns/iter",
            "extra": "iterations: 3485654\ncpu: 201.00290935359624 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 224.213654117647,
            "unit": "ns/iter",
            "extra": "iterations: 3127335\ncpu: 224.19299691270677 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 269.55435257524243,
            "unit": "ns/iter",
            "extra": "iterations: 2597715\ncpu: 269.5100894439921 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3353.4648912271246,
            "unit": "ns/iter",
            "extra": "iterations: 208324\ncpu: 3353.1297546130045 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "id": "f0e4723cfe73b0606d5bb7a40eee500f57be3d0a",
          "message": "fix(scripts): restart a tick that stages its own sources\n\nEnqueueing aurus-curves on 2026-09-08 failed on both hosts with\n`declaration unusable: ... phases[0]: unknown key(s) compare_timeout,\ncores, cuts, deadline_s, kind, maximal_timeout, out, results, workers`.\nThe file on disk was correct and the parser reading it was not: both\nhosts stood on campaign/aurus-rematch at f8fbe26, which predates the\n`kind = \"score\"` phase, and a tick keeps the campaign.py it imported\nbefore the checkout moved. ensure_staged runs before load_campaign\ndeliberately, the declaration being tracked on the campaign's own\nbranch and unreadable until then, so the staging tick always reads a\nnew declaration with old code. Parsing is the first thing that breaks\nrather than the only one: phase_command from that revision cannot\nrender a phase kind it does not define either.\n\nA tick now re-execs itself once, where staging changed the two commits'\ncontents under scripts/, and the restarted process runs the phase.\nensure_staged already returns the entry to `queued` before it returns,\nso the restart spends no attempt. The scripts/ diff is what keeps the\ncommon case as it was: a campaign sitting on another branch of the same\nscripts still runs its phase in the tick that staged it, rather than\npaying a five-minute cycle for nothing.\n\nThe queue lock's descriptor is carried across the exec — set\ninheritable, named in COUNTER_TICK_LOCK_FD, adopted on the other side —\nrather than dropped. flock is per open file description and Python\nopens the lock file O_CLOEXEC by default (PEP 446), so a bare exec\nwould release the lock for the length of the exec and let a hand-typed\ntick in between the two processes, and both would then run the same\nentry. That is the failure the lock exists to prevent, reached through\nthe mechanism that prevents it. COUNTER_TICK_RESTARTED marks the\nrestarted process, so scripts that move again run on what they have\ninstead of looping.\n\nrestart_after_stage returns 0 after a successful exec. That line is\nunreachable in production, the exec having replaced the process, and is\nthere so a stubbed exec stops the tick the way the real one does\ninstead of falling through into the phase. The tick fixture gains the\nstaging case, the no-second-restart case and the lock adoption; the\nhand-off was also checked against a real exec, where the child adopts\nthe descriptor and a third opener is refused.",
          "timestamp": "2026-09-08T15:30:52Z",
          "url": "https://github.com/benmandrew/counter/commit/f0e4723cfe73b0606d5bb7a40eee500f57be3d0a"
        },
        "date": 1788941011604,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 5.851188256320301,
            "unit": "ns/iter",
            "extra": "iterations: 135309274\ncpu: 5.849394558129106 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 91.8677078480289,
            "unit": "ns/iter",
            "extra": "iterations: 6872834\ncpu: 91.85923376004716 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 84.62901632582626,
            "unit": "ns/iter",
            "extra": "iterations: 9281244\ncpu: 84.62058566717995 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 47.214020778836804,
            "unit": "ns/iter",
            "extra": "iterations: 14942993\ncpu: 47.1938818414758 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 354.80136236672683,
            "unit": "ns/iter",
            "extra": "iterations: 1970835\ncpu: 354.7631871770086 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1437.781086069855,
            "unit": "ns/iter",
            "extra": "iterations: 493541\ncpu: 1437.7074873212152 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 390.1847356431409,
            "unit": "ns/iter",
            "extra": "iterations: 1785768\ncpu: 390.14805954636915 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 121.11033623537084,
            "unit": "ns/iter",
            "extra": "iterations: 5740562\ncpu: 121.10622932040448 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 137.85734723393105,
            "unit": "ns/iter",
            "extra": "iterations: 5109610\ncpu: 137.85065318096702 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 153.07345109532102,
            "unit": "ns/iter",
            "extra": "iterations: 4578897\ncpu: 153.06223704092935 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 210.2813363959103,
            "unit": "ns/iter",
            "extra": "iterations: 3638772\ncpu: 210.26805609145063 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 2341.324922132077,
            "unit": "ns/iter",
            "extra": "iterations: 297619\ncpu: 2341.1960291513656 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "355c26d92c21d77aa765ec08686f5040dd059f65",
          "message": "Merge pull request #175 from benmandrew/fix/fretish-path-parity\n\nfix(genetic): close four FRETISH parity gaps behind the TLSF path",
          "timestamp": "2026-09-09T19:55:18Z",
          "url": "https://github.com/benmandrew/counter/commit/355c26d92c21d77aa765ec08686f5040dd059f65"
        },
        "date": 1789027403835,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 9.343122627532045,
            "unit": "ns/iter",
            "extra": "iterations: 75011809\ncpu: 9.341481659241149 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 138.894299097102,
            "unit": "ns/iter",
            "extra": "iterations: 4967564\ncpu: 138.88519242026877 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 116.61969990741498,
            "unit": "ns/iter",
            "extra": "iterations: 6009612\ncpu: 116.61141900675119 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 80.90828020471143,
            "unit": "ns/iter",
            "extra": "iterations: 8727091\ncpu: 80.89704152277085 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 542.4161796485369,
            "unit": "ns/iter",
            "extra": "iterations: 1283662\ncpu: 542.3469223206733 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2276.8491867497905,
            "unit": "ns/iter",
            "extra": "iterations: 307347\ncpu: 2276.6190657465327 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 594.232159519509,
            "unit": "ns/iter",
            "extra": "iterations: 1168158\ncpu: 594.2012553096415 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 198.5452343812161,
            "unit": "ns/iter",
            "extra": "iterations: 3531661\ncpu: 198.53903729718118 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 225.7370583514774,
            "unit": "ns/iter",
            "extra": "iterations: 3127770\ncpu: 225.7317411446492 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 250.83140569698773,
            "unit": "ns/iter",
            "extra": "iterations: 2789513\ncpu: 250.81364130584794 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 302.62193148198907,
            "unit": "ns/iter",
            "extra": "iterations: 2273125\ncpu: 302.6126939785545 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3674.409277658994,
            "unit": "ns/iter",
            "extra": "iterations: 190824\ncpu: 3672.972005617742 ns\nthreads: 1"
          }
        ]
      }
    ],
    "PEREDUR benchmarks": [
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "23982e24abde25b73e789f73bcec12f4d1428726",
          "message": "Merge pull request #186 from benmandrew/campaign/fretish-maximal-curve\n\ndocs(experiments): close the 2026-09-09-fretish-maximal-curve archive",
          "timestamp": "2026-09-14T16:24:46Z",
          "url": "https://github.com/benmandrew/PEREDUR/commit/23982e24abde25b73e789f73bcec12f4d1428726"
        },
        "date": 1789461550888,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 9.37716568843229,
            "unit": "ns/iter",
            "extra": "iterations: 74957285\ncpu: 9.37329411010551 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 133.42480333222917,
            "unit": "ns/iter",
            "extra": "iterations: 5229886\ncpu: 133.387252035704 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 119.67197380380334,
            "unit": "ns/iter",
            "extra": "iterations: 5616085\ncpu: 119.66105623401356 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 78.36487394303973,
            "unit": "ns/iter",
            "extra": "iterations: 8667947\ncpu: 78.33822345706541 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 528.1839135060135,
            "unit": "ns/iter",
            "extra": "iterations: 1328369\ncpu: 527.9878776153313 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2285.0937611071695,
            "unit": "ns/iter",
            "extra": "iterations: 306673\ncpu: 2284.689372719478 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 650.2031029683535,
            "unit": "ns/iter",
            "extra": "iterations: 1092889\ncpu: 650.090042996133 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 210.3369640677023,
            "unit": "ns/iter",
            "extra": "iterations: 3360569\ncpu: 210.30835522198745 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 234.58683608781448,
            "unit": "ns/iter",
            "extra": "iterations: 2992621\ncpu: 234.5596709372818 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 260.595551707299,
            "unit": "ns/iter",
            "extra": "iterations: 2687503\ncpu: 260.56806448216065 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 311.6378411145254,
            "unit": "ns/iter",
            "extra": "iterations: 2238581\ncpu: 311.60705464756495 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3453.383430546601,
            "unit": "ns/iter",
            "extra": "iterations: 202819\ncpu: 3452.9128237492514 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "benmandrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "id": "80c307937d5a0f9e0a5615010f956c09facc5aa1",
          "message": "feat(examples): import the Lift-Plus-Cruise requirements\n\nAdds examples/lift-plus-cruise-mini/ and examples/lift-plus-cruise-full/,\nthe aircraft requirements from Pressburger, Katis, Dutle and Mavridou,\n\"Authoring, Analyzing, and Monitoring Requirements for a\nLift-Plus-Cruise Aircraft\" (REFSQ 2023), and its companion NASA\ntechnical report, \"Using FRET to Create, Analyze and Monitor\nRequirements for a Lift Plus Cruise Case Study\" (appendix tables 8.1\nand 8.2). Both transcribe the four state machines (lift mode, CR, DR,\nFCS): \"Upon\" transitions become trigger + NextTimepoint, and \"always if\npreBool(false, c) then r\" stays in the _pre form as continual +\nNextTimepoint. `within N ticks` lowers to F[0,N] unchanged, and xor is\nwritten !(a <-> b).\n\nkias and kgs are real-valued in the paper; here kias is threshold atoms\nkias_ge_N on a 10-knot grid, since every guard in the paper is a\nmultiple of 10 and the paper's witness traces land on the grid.\nKIAS_DERIVATIVE (|delta kias| <= 10 per tick) becomes one continual\nNextTimepoint requirement per level in each direction, and the\nlift_mode integer is four one-hot atoms lm_* with two non-weakenable\nguarantees for mutual exclusion and threshold monotonicity.\n\nlift-plus-cruise-mini is the REFSQ paper's section 4.2 set: 48\nguarantees, 20 outputs, no inputs, REACH_HOVER at 10 ticks, with kias\nfrom 30 to 120 knots and one band below. realize finds it\nUNREALIZABLE, matching the paper.\nfixes/paper-reach-hover-11.json raises REACH_HOVER to 11 ticks, the\npaper's own boundary, and realize confirms REALIZABLE.\n\nlift-plus-cruise-full is the technical report's complete set in the\nwind scenario of section 4.3: all six INIT_* guarantees, KIAS_0,\nKIAS_KGS_WIND_SPEED in place of KIAS_KGS, REACH_HOVER_16, and\nassumptions INIT_WIND_SPEED, WIND_SPEED_30, WIND_SPEED_DERIV (17\nassumptions, 59 guarantees, 8 inputs, 24 outputs). wind_speed is an\ninput on the same grid; kias reaches down to 0 with one band below;\nkgs = kias + wind_speed becomes outputs kgs_ge_20 and kgs_ge_30, each\na disjunction over wind levels, both needed because INIT_HOVER_MODE\ncompares kgs < 20 while the lift-mode guards use kgs <= 20.\nREACH_NOT_FCS_* and the _until stay forms are left out: the former is\nnever discussed in the text, and this grammar has no until timing (the\nreport calls the _until form equivalent). realize reproduces every\nclaim in section 4.3: |wind| <= 30 at 16 ticks UNREALIZABLE, the same\nwithout KIAS_0 REALIZABLE (backwards flight), |wind| <= 20 REALIZABLE\nat 13 ticks and UNREALIZABLE at 12. fixes/paper-wind-20.json is the\nreport's fix, tightening the wind assumption to |wind| <= 20 with 16\nticks kept.\n\nlint-ideals passes both ideals on all five checks. Neither example is\nregistered in scripts/run_experiments.py.",
          "timestamp": "2026-09-15T14:36:49Z",
          "url": "https://github.com/benmandrew/PEREDUR/commit/80c307937d5a0f9e0a5615010f956c09facc5aa1"
        },
        "date": 1789547372107,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 4.044651371970208,
            "unit": "ns/iter",
            "extra": "iterations: 180021053\ncpu: 4.0444807141529155 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 64.32575664796047,
            "unit": "ns/iter",
            "extra": "iterations: 9471776\ncpu: 64.31969199862836 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 77.27184088750276,
            "unit": "ns/iter",
            "extra": "iterations: 8916911\ncpu: 77.26748422183424 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 39.888493649056194,
            "unit": "ns/iter",
            "extra": "iterations: 18542047\ncpu: 39.86979700784926 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 275.10077537283837,
            "unit": "ns/iter",
            "extra": "iterations: 2554771\ncpu: 274.9787546515909 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 1171.5283593317974,
            "unit": "ns/iter",
            "extra": "iterations: 592856\ncpu: 1171.4396109679249 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 324.15942401865595,
            "unit": "ns/iter",
            "extra": "iterations: 2118263\ncpu: 324.01116197563766 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 111.7517562259578,
            "unit": "ns/iter",
            "extra": "iterations: 6261011\ncpu: 111.74288321806176 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 124.30786913828386,
            "unit": "ns/iter",
            "extra": "iterations: 5566823\ncpu: 124.29897986697266 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 140.03561628744163,
            "unit": "ns/iter",
            "extra": "iterations: 5119259\ncpu: 139.98402366436233 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 168.50158756421803,
            "unit": "ns/iter",
            "extra": "iterations: 4158257\ncpu: 168.48071150965427 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 1797.2218505753442,
            "unit": "ns/iter",
            "extra": "iterations: 383578\ncpu: 1797.084796312616 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Ben Andrew",
            "username": "benmandrew",
            "email": "benmandrew@gmail.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "4114f52fe2cd9ce3c7d58079d1581284f4d7bf7a",
          "message": "Merge pull request #189 from benmandrew/feat/fret-stop-timings\n\nfeat(fretish): add FRET's until/before stop timings",
          "timestamp": "2026-09-18T13:28:48Z",
          "url": "https://github.com/benmandrew/PEREDUR/commit/4114f52fe2cd9ce3c7d58079d1581284f4d7bf7a"
        },
        "date": 1789804763552,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "Copy formula - 8 variables",
            "value": 6.932636080632825,
            "unit": "ns/iter",
            "extra": "iterations: 100978789\ncpu: 6.928158120414773 ns\nthreads: 1"
          },
          {
            "name": "Copy specification - 3-guarantee takeoff spec",
            "value": 150.01798569550195,
            "unit": "ns/iter",
            "extra": "iterations: 4711522\ncpu: 149.9573694020743 ns\nthreads: 1"
          },
          {
            "name": "Hash specification - 3-guarantee takeoff spec",
            "value": 106.86475830070403,
            "unit": "ns/iter",
            "extra": "iterations: 6556639\ncpu: 106.86226266231832 ns\nthreads: 1"
          },
          {
            "name": "Compare specifications - equal, distinct arenas",
            "value": 80.5463296450453,
            "unit": "ns/iter",
            "extra": "iterations: 8820087\ncpu: 80.54414621987289 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - small formulas (3 variables)",
            "value": 507.3585777803332,
            "unit": "ns/iter",
            "extra": "iterations: 1374190\ncpu: 507.34472525633254 ns\nthreads: 1"
          },
          {
            "name": "Syntactic similarity - large formulas (11 variables, O(n*m) shared_subformulae)",
            "value": 2033.8015881097813,
            "unit": "ns/iter",
            "extra": "iterations: 343679\ncpu: 2033.7404322056345 ns\nthreads: 1"
          },
          {
            "name": "Spec implication check - warm black cache",
            "value": 672.0231865586726,
            "unit": "ns/iter",
            "extra": "iterations: 1041983\ncpu: 671.9978013076988 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:5",
            "value": 165.5119309849577,
            "unit": "ns/iter",
            "extra": "iterations: 4232006\ncpu: 165.506521257295 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:10",
            "value": 187.4913780956322,
            "unit": "ns/iter",
            "extra": "iterations: 3732934\ncpu: 187.4848245374817 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:20",
            "value": 208.41766965162225,
            "unit": "ns/iter",
            "extra": "iterations: 3361300\ncpu: 208.40625472287488 ns\nthreads: 1"
          },
          {
            "name": "Trace model counting - matrix exponentiation/steps:50",
            "value": 261.9150999227026,
            "unit": "ns/iter",
            "extra": "iterations: 2695133\ncpu: 261.9024660378541 ns\nthreads: 1"
          },
          {
            "name": "Mutate specification - 3-guarantee takeoff spec",
            "value": 3336.9012157981088,
            "unit": "ns/iter",
            "extra": "iterations: 208505\ncpu: 3336.7384427231923 ns\nthreads: 1"
          }
        ]
      }
    ]
  }
}