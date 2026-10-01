# fretish-mixed

This plan is pre-registered. It fixes the question, design, corpus and cost before any row of the campaign exists; the endpoint is fixed before launch.

## Question

Does a per-firing coin flip between *directed mutation* and a *uniform redraw* of the ordered FRETISH fields reach the union of both arms' repair frontiers? The three fields are timing, condition type and scope. The `2026-09-29-fretish-ablation` campaign found uniform ahead at the cap, 21 uniform-only pairs against 1 directed-only, with all 21 on `rad-core-10`. Directed was faster on 377 of 570 both-finished pairs. Neither arm dominates, so a search that takes both rules may keep the reach of one and the speed of the other.

The config key is `[mutation] ordered_fields = "mixed"`. Each time the timing, condition-type or scope arm fires, a fair coin picks the directed or the uniform rule for that field, and the three arms flip independently. The *monotone rewrite* of condition and response (`p_monotone`) stays directed, as in both other arms.

## Design

This campaign runs one arm, `mixed`, as the third level of sweep O. Every other key sits where the ablation put it: `nsga2-apportion` selection, `mrs` status grading, weights 0.1/0.2/0.7, the log metric, weakening off, 1000 individuals at population 100, `parallel = 1`, and the runner's 7200 s cap as the *censoring point*. Each cell runs 30 seeds (0–29) at 16 concurrent runs per host, with seeds 0–14 on av2 and 15–29 on av3, the ablation's split.

The directed and uniform rows are reused from `2026-09-29-fretish-ablation` rather than re-run. The reuse is valid only once byte-identity is shown at this campaign's commit. The coin is drawn only under `mixed`, so the two older arms should draw the streams they drew at `dc3e276`. The existing determinism goldens pin both streams and must pass unchanged at the new commit. A paired spot check then re-runs a small sample of directed and uniform cells and compares their outputs against the archived rows. Until both hold, the reused rows are a different binary's output and the comparison does not stand.

## Corpus

The run profile `fretish-mixed` holds the ablation's 20 subjects. Eight are whole specs: `takeoff`, `fsm`, `fsm-timing`, `fsm-combined`, `fsm-lmcps`, `liquid-mixer`, `mode-arbiter` and `valu3s-uc6`. The other 12 are *unrealisable cores*: 10 from `rad`, `lpc-mini-core1` and `lpc-full-core1`.

`implies_ideal` is readable on the same 6 subjects as in the ablation: `takeoff`, `fsm`, `fsm-timing`, `fsm-combined`, `fsm-lmcps` and `liquid-mixer`.

The campaign is 20 subjects × 1 arm × 30 seeds, or 600 runs.

## Endpoints and tests

TODO: endpoint to be fixed before launch.

## Cost

The campaign is half the ablation's 1200 runs. That campaign's calibration put it at 98–177 core-hours, which halves to about 50–90 core-hours here. The ablation spent 212.5 core-hours in the event, almost all of it on `lpc-full-core1` runs killed at the cap, so half of the realised figure is about 106 core-hours. The estimate excludes the score phase.

## Follow-up

A mixed arm that reaches the union would make the coin a candidate shipping default. A null result keeps `directed` as the default and leaves the ablation's reading as it stands.
