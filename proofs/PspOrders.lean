import Psp

/-!
Prints the transcribed C++ tables of `Psp/Cpp.lean` for every PSP pattern with
counts from 1 to 4 and every scope, as three kinds of tab-separated line:

* `pattern <scope> <stronger> <weaker> 0|1`: `patternImplies` at the scope's class;
* `scope <pattern> <stronger> <weaker> 0|1`: `scopeImplies` in the pattern's cell;
* `polarity <pattern>/<scope> <field> +|-|none`: `fieldPolarity`, for each field read.

`scripts/check_lean_psp_orders.py` compares the first two with the C++
helpers `psp_pattern_order_table` and `psp_scope_order_table`, and all three
with what SPOT measured in `docs/dev/psp-orders.json`.

    lake env lean --run PspOrders.lean
-/

open Psp

def pats : List Pat :=
  [.absence, .universality, .existence, .precedence, .response] ++
    (List.range 4).flatMap (fun i => [.bresp (i + 1), .binv (i + 1), .bexist (i + 1), .brec (i + 1)])

def Psp.Pat.label (a : Pat) : String :=
  let name := match a with
    | .absence => "absence"
    | .universality => "universality"
    | .existence => "existence"
    | .precedence => "precedence"
    | .response => "response"
    | .bresp _ => "bounded-response"
    | .binv _ => "bounded-invariance"
    | .bexist _ => "bounded-existence"
    | .brec _ => "bounded-recurrence"
  match a.count with
  | some n => s!"{name} {n}"
  | none => name

def scopes : List (Scope × String) :=
  [(.globally, "globally"), (.before, "before"), (.after, "after"), (.between, "between"),
   (.afterUntil, "afteruntil")]

def bit (b : Bool) : String := if b then "1" else "0"

def Psp.Pol.sign : Pol → String
  | .monotone => "+"
  | .antitone => "-"
  | .none => "none"

def main : IO Unit := do
  for (k, kname) in scopes do
    for a in pats do
      for b in pats do
        IO.println s!"pattern\t{kname}\t{a.label}\t{b.label}\t{bit (patternImplies a b k.cls')}"
  for a in pats do
    for (k, kname) in scopes do
      for (k', kname') in scopes do
        IO.println s!"scope\t{a.label}\t{kname}\t{kname'}\t{bit (scopeImplies k k' a.cell')}"
  for a in pats do
    for (k, kname) in scopes do
      for (f, fname) in [(FField.P, "P"), (.S, "S"), (.Q, "Q"), (.R, "R")] do
        match fieldPolarity f a k with
        | some p => IO.println s!"polarity\t{a.label}/{kname}\t{fname}\t{p.sign}"
        | none => pure ()
