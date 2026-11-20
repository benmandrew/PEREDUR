import Psp.ScopeExact

/-!
# The C++ tables, transcribed

`patternImplies`, `scopeImplies`, `Scope.cls'`, `Pat.cell'` and
`fieldPolarity` transcribe `pattern_implies`, `scope_implies`, `scope_class`,
`pattern_cell` and `field_polarity` in `src/psp/implication.cpp`, branch for
branch, with `Pat.usesP`, `Pat.usesS`, `Scope.usesQ` and `Scope.usesR` for
the helpers in `src/psp/requirement.cpp`. The first four are proved equal to
the exact orders, so `pattern_implies_exact` and `scope_implies_exact` state
the C++ functions' correctness at every count. `Psp.Polarity` proves
`fieldPolarity` sound. `scripts/check_lean_psp_orders.py` compares the
printed tables with the C++ helpers' and with `docs/dev/psp-orders.json`.
-/

namespace Psp

/-- The variant index, as `std::variant::index`. -/
def Pat.index : Pat → Nat
  | .absence => 0
  | .universality => 1
  | .existence => 2
  | .precedence => 3
  | .response => 4
  | .bresp _ => 5
  | .binv _ => 6
  | .bexist _ => 7
  | .brec _ => 8

/-- `pattern_count` -/
def Pat.count : Pat → Option Nat
  | .bresp n | .binv n | .bexist n | .brec n => some n
  | _ => none

def Pat.isBinv : Pat → Bool
  | .binv _ => true
  | _ => false

/-- `count_implies` -/
def countImplies (stronger weaker : Pat) : Bool :=
  let lhs := stronger.count.getD 0
  let rhs := weaker.count.getD 0
  if stronger.isBinv then decide (lhs ≥ rhs) else decide (lhs ≤ rhs)

/-- `cross_pattern_implies` -/
def crossPatternImplies (stronger weaker : Pat) (inClass : Cls) : Bool :=
  let weakerIsTriggered :=
    (weaker matches .bresp _) || (weaker matches .binv _) || (weaker matches .precedence) ||
      (weaker matches .response)
  match stronger with
  | .absence => weakerIsTriggered || (weaker matches .bexist _)
  | .universality =>
    weakerIsTriggered || (weaker matches .brec _) ||
      ((weaker matches .existence) && inClass != .before)
  | .binv _ => (weaker matches .bresp _) || (weaker matches .precedence) ||
      (weaker matches .response)
  | .bresp _ => (weaker matches .response)
  | .brec c =>
    if inClass != .unbounded then false
    else (match weaker with
      | .bresp n => decide (n ≥ c)
      | w => (w matches .existence) || (w matches .response))
  | _ => false

/-- `pattern_implies` -/
def patternImplies (stronger weaker : Pat) (inClass : Cls) : Bool :=
  if stronger.index = weaker.index then stronger.count.isNone || countImplies stronger weaker
  else crossPatternImplies stronger weaker inClass

/-- `scope_class` -/
def Scope.cls' : Scope → Cls
  | .globally | .after => .unbounded
  | .between | .afterUntil => .bounded
  | .before => .before

/-- `pattern_cell` -/
def Pat.cell' : Pat → Cell
  | .existence => .existence
  | .precedence => .precedence
  | .response | .bresp _ => .response
  | _ => .invariant

/-- `scope_implies` -/
def scopeImplies (stronger weaker : Scope) (cell : Cell) : Bool :=
  if stronger = weaker then true
  else if stronger = .afterUntil && weaker = .between then true
  else match cell with
    | .invariant => stronger = .globally ||
        (stronger = .after && (weaker = .afterUntil || weaker = .between))
    | .existence => false
    | .precedence => stronger = .globally && weaker = .before
    | .response => stronger = .globally && weaker = .after

/-! ## Polarity -/

/-- `pattern_uses_p` -/
def Pat.usesP (a : Pat) : Bool :=
  !(a matches .universality) && !(a matches .existence) && !(a matches .brec _)

/-- `pattern_uses_s` -/
def Pat.usesS (a : Pat) : Bool := !(a matches .absence) && !(a matches .bexist _)

/-- `scope_uses_q` -/
def Scope.usesQ (k : Scope) : Bool := k = .after || k = .between || k = .afterUntil

/-- `scope_uses_r` -/
def Scope.usesR (k : Scope) : Bool := k = .before || k = .between || k = .afterUntil

inductive FField where
  | P
  | S
  | Q
  | R
  deriving DecidableEq, Repr

/-- `Polarity` -/
inductive Pol where
  | monotone
  | antitone
  | none
  deriving DecidableEq, Repr

/-- `field_polarity` -/
def fieldPolarity (field : FField) (a : Pat) (k : Scope) : Option Pol :=
  match field with
  | .P => if !a.usesP then .none else if a matches .bexist _ then some .none else some .antitone
  | .S => if !a.usesS then .none else some .monotone
  | .Q =>
    if !k.usesQ then .none
    else if k = .after && ((a matches .existence) || (a matches .precedence)) then some .none
    else some .antitone
  | .R =>
    if !k.usesR then .none
    else if k = .afterUntil then
      if a.cell' = .existence || a.cell' = .response then some .none else some .monotone
    else if k = .before && (a matches .existence) then some .antitone
    else some .none

/-! ## Against the proved orders -/

theorem cls_eq (k : Scope) : k.cls' = k.cls := by cases k <;> rfl

theorem cell_eq (a : Pat) : a.cell' = a.cell := by cases a <;> rfl

theorem patternImplies_iff (a b : Pat) (c : Cls) : patternImplies a b c = true ↔ patLe c a b := by
  cases a <;> cases b <;> cases c <;>
    simp [patternImplies, crossPatternImplies, countImplies, Pat.index, Pat.count, Pat.isBinv,
      patLe] <;> omega

theorem scopeImplies_iff (k k' : Scope) (c : Cell) : scopeImplies k k' c = true ↔ scopeLe c k k' := by
  cases k <;> cases k' <;> cases c <;> simp [scopeImplies, scopeLe]

/-- Under every scope, for patterns the C++ accepts, `pattern_implies` at the
scope's class returns true exactly when the one pattern implies the other over
every P, S, Q and R, at every count. -/
theorem pattern_implies_exact (k : Scope) {a b : Pat} (ha : a.Valid) (hb : b.Valid) :
    patternImplies a b k.cls' = true ↔ Implies k a k b := by
  rw [cls_eq, patternImplies_iff]
  exact (pattern_exact k ha hb).symm

/-- For every pattern the C++ accepts, `scope_implies` in the pattern's cell
returns true exactly when the one scope implies the other over every P, S, Q
and R, at every count. -/
theorem scope_implies_exact {a : Pat} (ha : a.Valid) (k k' : Scope) :
    scopeImplies k k' a.cell' = true ↔ Implies k a k' a := by
  rw [cell_eq, scopeImplies_iff]
  exact (scope_exact ha k k').symm

end Psp
