import Fretish.Cpp

/-!
Prints `timingImplies` for every scope and every ordered pair of timings with
tick counts up to 4, leaving out `within 0` and `for 0`, which
`timing_implies` rejects. Each line is the scope, the stronger timing, the
weaker timing and `1` or `0`, separated by tabs.
`scripts/check_lean_timing_order.py` compares it with the C++ table.

    lake env lean --run TimingOrder.lean
-/

open Fretish

def scopes : List (ScopeKind × String) :=
  [(.global, "Global"), (.inMode, "In"), (.notInMode, "NotIn"), (.beforeMode, "Before"),
   (.afterMode, "After"), (.onlyIn, "OnlyIn"), (.onlyBefore, "OnlyBefore"),
   (.onlyAfter, "OnlyAfter")]

def kinds : List (Kind × String) :=
  [(.basic .imm, "immediately"), (.basic .next, "next")] ++
  (List.range 5).flatMap (fun n =>
    (if n = 0 then [] else
      [(.basic (.within n), s!"within {n}"), (.basic (.forT n), s!"for {n}")]) ++
    [(.basic (.after n), s!"after {n}")]) ++
  [(.basic .ev, "eventually"), (.basic .alw, "always"), (.untilK, "until"),
   (.beforeK, "before")]

def main : IO Unit := do
  for (k, kname) in scopes do
    for (K, a) in kinds do
      for (K', b) in kinds do
        let v := if timingImplies K K' k then "1" else "0"
        IO.println s!"{kname}\t{a}\t{b}\t{v}"
