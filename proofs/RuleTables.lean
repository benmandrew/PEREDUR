import Fretish.Rules

/-!
Prints the four timing tables of `Fretish.Rules` for every ordered pair of
timings with tick counts up to 4, `within 0` and `for 0` included. Each line
is the panel, the stronger timing, the weaker timing and `1` or `0`, separated
by tabs. The panels are named by a scope they cover: `global` for `le0K`,
`in` for `leBK`, `onlyIn` for `leO leBK` and `onlyBefore` for `leO le0K`.
`scripts/lean_rule_diagrams.py` reduces each table to the covering pairs a
Hasse diagram draws.

    lake env lean --run RuleTables.lean
-/

open Fretish

def panels : List (ScopeKind × String) :=
  [(.global, "global"), (.inMode, "in"), (.onlyIn, "onlyIn"), (.onlyBefore, "onlyBefore")]

def kinds : List (Kind × String) :=
  [(.basic .imm, "immediately"), (.basic .next, "next")] ++
  (List.range 5).flatMap (fun n =>
    [(.basic (.within n), s!"within {n}"), (.basic (.forT n), s!"for {n}"),
     (.basic (.after n), s!"after {n}")]) ++
  [(.basic .ev, "eventually"), (.basic .alw, "always"), (.untilK, "until"),
   (.beforeK, "before")]

def main : IO Unit := do
  for (k, kname) in panels do
    for (K, a) in kinds do
      for (K', b) in kinds do
        let v := if decide (timingTable k K K') then "1" else "0"
        IO.println s!"{kname}\t{a}\t{b}\t{v}"
