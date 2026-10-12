# Examples

Each directory holds one specification. `spec.tlsf` is a specification in the basic Temporal Logic Synthesis Format (TLSF), and `spec.json` is a set of FRETISH requirements, the second input format that the tools read. Where a directory has a `fixes/` folder, each file in it is a known-good repair, called an *ideal*, that `compare` scores a run's repairs against.

The container image installs this directory at `/opt/peredur/share/peredur/examples`.

## The paper's running example

| Example | Origin |
|---|---|
| [`paper-running-example`](paper-running-example/spec.tlsf) | Written for the paper. A two-client arbiter with mutual exclusion and two immediate grants, whose three guarantees conflict only together. |

## The paper's evaluation subjects

These 25 specifications are the subjects of the paper's evaluation. They are the corpus that Brizzio et al. assembled to evaluate AuRUS (2023, section 6), less `humanoid-741`. That paper gives three sources: the literature, the reactive synthesis competition (SYNTCOMP), and specifications that students wrote, reported in SYNTECH15. The split below follows its Table 1.

Eight of the AuRUS files use a `GLOBAL` block that the parser here rejects, so their `spec.tlsf` is the output of `syfco -f basic` on the AuRUS copy. A name that ends in `-aurus` marks the instance that AuRUS used, where another instance of the same family is also bundled.

| Source | Examples |
|---|---|
| Literature (5) | `arbiter-aurus`, `lift`, `minepump`, `rg1`, `rg2` |
| SYNTCOMP (13) | `detector-aurus`, `full-arbiter-aurus`, `lily02`, `lily11`, `lily15`, `lily16`, `load-balancer-aurus`, `ltl2dba-r-2`, `ltl2dba-theta-2`, `ltl2dba27`, `prioritized-arbiter-aurus`, `round-robin-arbiter-aurus`, `simple-arbiter-aurus` |
| SYNTECH15 (7) | `gyro-var1`, `gyro-var2`, `humanoid-458`, `humanoid-503`, `humanoid-531`, `humanoid-742`, `pcar-v2-888` |

`humanoid-741` is the 26th specification of that corpus (SYNTECH15). It is bundled without ideals and held out of the evaluation. Its assumptions are not well-separated, and no edit that the search can make repairs that.

Some ideals in `fixes/` are the reference repairs from the AuRUS repository, and the others were written by hand for this project. `lint-ideals` checks every one.

## Other TLSF specifications

| Example | Origin |
|---|---|
| `arbiter` | Written for this project. A two-client arbiter in GR(1) form that needs a fairness assumption on the requests. It is a different specification from `arbiter-aurus`. |
| `arbiter-handshake` | Written for this project. A two-client arbiter with transient requests, whose ideal is a hold-until-grant assumption that names the grant outputs. |
| `detector`, `full-arbiter`, `load-balancer`, `prioritized-arbiter`, `round-robin-arbiter`, `simple-arbiter` | SYNTCOMP 2019 unrealisable instances in basic format, taken from the AuRUS repository: `detector_unreal_4`, `full_arbiter_unreal1_3_4`, `load_balancer_unreal1_2_2`, `prioritized_arbiter_unreal1_3_4`, `round_robin_arbiter_unreal1_2_3` and `simple_arbiter_unreal2_3`. Each differs from its `-aurus` counterpart. |
| `amba` | The AuRUS repository's case studies: the AMBA AHB specification with its fairness assumption removed (`amba_ahb_wo_ass_fairness_amba_ahb_1`). The ideal was rewritten here to pass `lint-ideals`. |
| `codesample-un1`, `codesample-un2` | The AuRUS repository's case studies: two unrealisable variants of the Forklift specification. |
| `takeoff-tlsf` | The AuRUS repository's case studies: the TLSF form of `takeoff`. |

## FRETISH specifications

| Example | Origin |
|---|---|
| `takeoff`, `fsm`, `fsm-timing`, `fsm-combined` | Written for this project. Small requirement sets that the first experiments ran on; `fsm-combined` composes the state machines of the other two `fsm` examples. |
| `mode-arbiter` | Written for this project. The one example whose requirements carry FRETISH scopes. |
| `fsm-lmcps` | The Finite State Machine challenge of the Ten Lockheed Martin Cyber-Physical Challenges (Mavridou et al., RE 2020). The 13 requirements are transcribed from section 4 of the 2019 NASA report "Evaluation of the FRET and CoCoSim tools on the Ten Lockheed Martin Cyber-Physical Challenge Problems". |
| `lift-plus-cruise-mini`, `lift-plus-cruise-full` | Pressburger, Katis, Dutle and Mavridou, "Authoring, Analyzing, and Monitoring Requirements for a Lift-Plus-Cruise Aircraft" (REFSQ 2023), and tables 8.1 and 8.2 of its companion NASA technical report. |
| `liquid-mixer` | The liquid mixer of Mavridou et al. (FM 2021), Table 1. |
| `rad` | The Robot-Assisted Dressing case study of the CRADLE project ([cradlerobotics/RAD](https://github.com/cradlerobotics/RAD), commit `5ab8545`). Five of its six FRET project exports are imported. |
| `valu3s-uc6` | The FRET project "VALU3S Use Case 6", imported from an export that is not public. |
| `ventilator-controller` | The Controller component of the FRET project "Ventilator v0.6.1", from Farrell, Luckcuck, Monahan, Reynolds and Sheridan, "FRETting and Formal Modelling: A Mechanical Lung Ventilator" (ABZ 2024). |

The FRET exports were converted with `scripts/import_fret.py`, whose docstring lists every conversion rule.
