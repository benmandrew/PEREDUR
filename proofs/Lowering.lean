import Fretish.Syntax

/-!
Prints `lowerSyn` for every scope, condition type and timing with tick counts
up to 3, over the atoms `c`, `r`, `s` and mode `m`, plus the literal `true`
condition. Each line is the requirement as `ltl` driver JSON, a tab, and the
formula in SPOT syntax. `scripts/check_lean_lowering.py` reads it.

    lake env lean --run Lowering.lean
-/

open Fretish

def scopes : List (ScopeKind × String) :=
  [(.global, "Global"), (.inMode, "In"), (.notInMode, "NotIn"), (.beforeMode, "Before"),
   (.afterMode, "After"), (.onlyIn, "OnlyIn"), (.onlyBefore, "OnlyBefore"),
   (.onlyAfter, "OnlyAfter")]

def ticksJson (kind : String) (n : Nat) : String :=
  "{\"type\":\"" ++ kind ++ "\",\"ticks\":" ++ toString n ++ "}"

def timings : List (STiming × String) :=
  [(.immediately, "{\"type\":\"Immediately\"}"), (.nextTick, "{\"type\":\"NextTimepoint\"}")] ++
  (List.range 4).flatMap (fun n =>
    [(.within n, ticksJson "WithinTicks" n), (.forTicks n, ticksJson "ForTicks" n),
     (.afterTicks n, ticksJson "AfterTicks" n)]) ++
  [(.eventually, "{\"type\":\"Eventually\"}"), (.always, "{\"type\":\"Always\"}"),
   (.untilStop (.atom "s"), "{\"type\":\"Until\",\"stop\":\"s\"}"),
   (.beforeStop (.atom "s"), "{\"type\":\"Before\",\"stop\":\"s\"}")]

def main : IO Unit := do
  for (k, kname) in scopes do
    for (ct, ctname) in [(CondType.continual, "continual"), (CondType.trigger, "trigger")] do
      for (c, cname) in [(LTL.atom "c", "c"), (LTL.tt, "true")] do
        for (T, tjson) in timings do
          let scope :=
            if kname == "Global" then ""
            else ",\"scope\":{\"type\":\"" ++ kname ++ "\",\"mode\":\"m\"}"
          let json := "{\"condition\":\"" ++ cname ++ "\",\"condition-type\":\"" ++ ctname ++
            "\",\"response\":\"r\",\"timing\":" ++ tjson ++ scope ++ "}"
          let φ := lowerSyn k ct T (.atom "m") c (.atom "r")
          IO.println (json ++ "\t" ++ φ.render)
