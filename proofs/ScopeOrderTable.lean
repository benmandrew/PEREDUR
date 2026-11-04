import Fretish.ScopeOrder

/-!
Prints `scopeImplies` for every condition type, timing with tick counts up to
4 (less `within 0` and `for 0`) and ordered pair of scopes, with the weaker
scope at the stronger's mode `a` or at mode `b`. A global scope carries the
empty mode, as in the C++. Each line is the condition type, the timing, the
stronger scope, the weaker scope, the weaker's mode and `1` or `0`, separated
by tabs. `scripts/check_lean_scope_order.py` compares it with the C++ table.

    lake env lean --run ScopeOrderTable.lean
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

def types : List (CondType × String) := [(.continual, "continual"), (.trigger, "trigger")]

def modeOf (k : ScopeKind) (mode : String) : String := if k = .global then "" else mode

def main : IO Unit := do
  for (ct, cname) in types do
    for (K, kname) in kinds do
      for (k, a) in scopes do
        for (k', b) in scopes do
          for mode in ["a", "b"] do
            let same := modeOf k "a" == modeOf k' mode
            let v := if scopeImplies k k' same K ct then "1" else "0"
            IO.println s!"{cname}\t{kname}\t{a}\t{b}\t{mode}\t{v}"
