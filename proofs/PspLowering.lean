import Psp

/-!
Prints `lowerSyn` from `Psp/Syntax.lean` for every PSP pattern, with counts
from 1 to 4, under every scope, over the atoms `p`, `s`, `q` and `r`. Each line
is the requirement as `.psp` JSON, with P, S, Q and R present exactly where
the pattern and scope read them, a tab, and the formula in SPOT syntax.
`scripts/check_lean_psp_lowering.py` reads it.

    lake env lean --run PspLowering.lean
-/

open Psp

def pats : List Pat :=
  [.absence, .universality, .existence, .precedence, .response] ++
    (List.range 4).flatMap (fun i => [.bresp (i + 1), .binv (i + 1), .bexist (i + 1), .brec (i + 1)])

def Psp.Pat.name : Pat → String
  | .absence => "absence"
  | .universality => "universality"
  | .existence => "existence"
  | .precedence => "precedence"
  | .response => "response"
  | .bresp _ => "bounded-response"
  | .binv _ => "bounded-invariance"
  | .bexist _ => "bounded-existence"
  | .brec _ => "bounded-recurrence"

def scopes : List (Scope × String) :=
  [(.globally, "globally"), (.before, "before"), (.after, "after"), (.between, "between"),
   (.afterUntil, "afteruntil")]

def field (used : Bool) (name : String) : String :=
  if used then s!",\"{name}\":\"{name}\"" else ""

def main : IO Unit := do
  for a in pats do
    for (k, kname) in scopes do
      let count := match a.count with
        | some n => s!",\"count\":{n}"
        | none => ""
      let json := s!"\{\"pattern\":\"{a.name}\"{count},\"scope\":\"{kname}\"" ++
        field a.usesP "p" ++ field a.usesS "s" ++ field k.usesQ "q" ++ field k.usesR "r" ++ "}"
      let φ := lowerSyn k a (.atom "p") (.atom "s") (.atom "q") (.atom "r")
      IO.println (json ++ "\t" ++ φ.render)
