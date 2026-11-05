import Fretish.Scope

/-!
# The lowering as syntax

`lowerSyn` builds an LTL formula the way `requirement_to_ltl` builds its
string: each `*Syn` function below follows one C++ function, branch for
branch, including the empty-boundary branches and `within 0`. `evalL` is the
textbook semantics of LTL over infinite traces, written without the
`Fretish.Trace` abbreviations.

`eval_lowerSyn` proves that the formula's meaning is `lower`, so every theorem
about `lower` holds of the formula. `LTL.render` prints it in SPOT syntax, and
`Lowering.lean` prints it for every small requirement, for
`scripts/check_lean_lowering.py` to compare with the C++ output.
-/

namespace Fretish

inductive LTL where
  | tt
  | atom (name : String)
  | not (φ : LTL)
  | and (φ ψ : LTL)
  | or (φ ψ : LTL)
  | imp (φ ψ : LTL)
  | X (φ : LTL)
  | F (φ : LTL)
  | G (φ : LTL)
  | U (φ ψ : LTL)
  | R (φ ψ : LTL)
  | W (φ ψ : LTL)
  deriving DecidableEq, Repr

def evalL (σ : String → Pred) : LTL → Pred
  | .tt => fun _ => True
  | .atom a => σ a
  | .not φ => fun t => ¬ evalL σ φ t
  | .and φ ψ => fun t => evalL σ φ t ∧ evalL σ ψ t
  | .or φ ψ => fun t => evalL σ φ t ∨ evalL σ ψ t
  | .imp φ ψ => fun t => evalL σ φ t → evalL σ ψ t
  | .X φ => fun t => evalL σ φ (t + 1)
  | .F φ => fun t => ∃ j, evalL σ φ (t + j)
  | .G φ => fun t => ∀ j, evalL σ φ (t + j)
  | .U φ ψ => fun t => ∃ j, evalL σ ψ (t + j) ∧ ∀ i, i < j → evalL σ φ (t + i)
  | .R φ ψ => fun t => ∀ j, evalL σ ψ (t + j) ∨ ∃ i, i < j ∧ evalL σ φ (t + i)
  | .W φ ψ => fun t =>
      (∃ j, evalL σ ψ (t + j) ∧ ∀ i, i < j → evalL σ φ (t + i)) ∨ ∀ j, evalL σ φ (t + j)

/-- SPOT syntax, fully parenthesised. -/
def LTL.render : LTL → String
  | .tt => "true"
  | .atom a => a
  | .not φ => "!(" ++ φ.render ++ ")"
  | .and φ ψ => "((" ++ φ.render ++ ") & (" ++ ψ.render ++ "))"
  | .or φ ψ => "((" ++ φ.render ++ ") | (" ++ ψ.render ++ "))"
  | .imp φ ψ => "((" ++ φ.render ++ ") -> (" ++ ψ.render ++ "))"
  | .X φ => "X(" ++ φ.render ++ ")"
  | .F φ => "F(" ++ φ.render ++ ")"
  | .G φ => "G(" ++ φ.render ++ ")"
  | .U φ ψ => "((" ++ φ.render ++ ") U (" ++ ψ.render ++ "))"
  | .R φ ψ => "((" ++ φ.render ++ ") R (" ++ ψ.render ++ "))"
  | .W φ ψ => "((" ++ φ.render ++ ") W (" ++ ψ.render ++ "))"

/-- A timing whose stop is a formula. -/
inductive STiming where
  | immediately
  | nextTick
  | within (n : Nat)
  | forTicks (n : Nat)
  | afterTicks (n : Nat)
  | eventually
  | always
  | untilStop (s : LTL)
  | beforeStop (s : LTL)

def STiming.sem (σ : String → Pred) : STiming → Timing
  | .immediately => .immediately
  | .nextTick => .nextTick
  | .within n => .within n
  | .forTicks n => .forTicks n
  | .afterTicks n => .afterTicks n
  | .eventually => .eventually
  | .always => .always
  | .untilStop s => .untilStop (evalL σ s)
  | .beforeStop s => .beforeStop (evalL σ s)

/-! ## Timing bodies -/

/-- `expand_within`: `r | X(r | X(... | X r))`, `n` nestings. -/
def expandWithin (r : LTL) : Nat → LTL
  | 0 => r
  | n + 1 => .or r (.X (expandWithin r n))

/-- `expand_for`: `r & X(r & X(... & X r))`, `n` nestings. -/
def expandFor (r : LTL) : Nat → LTL
  | 0 => r
  | n + 1 => .and r (.X (expandFor r n))

def afterChain (r : LTL) : Nat → LTL
  | 0 => r
  | k + 1 => .and (.not r) (.X (afterChain r k))

/-- `expand_after`: `!r & X(!r & X(... & X r))`, `n + 1` nestings. -/
def expandAfter (r : LTL) (n : Nat) : LTL := afterChain r (n + 1)

/-- `within_body`; `none` is the empty boundary. -/
def withinSyn (n : Nat) (ob : Option LTL) (r : LTL) : LTL :=
  match ob, n with
  | none, _ => expandWithin r n
  | some _, 0 => expandWithin r 0
  | some b, k + 1 => .or (expandWithin r (k + 1)) (expandWithin b k)

/-- `for_body` -/
def forSyn (n : Nat) (ob : Option LTL) (r : LTL) : LTL :=
  match ob with
  | none => expandFor r n
  | some b => .or (expandFor r n) (.R b r)

/-- `timing_body` -/
def timingSyn : STiming → Option LTL → LTL → LTL
  | .immediately, _, r => r
  | .nextTick, none, r => .X r
  | .nextTick, some b, r => .or b (.X r)
  | .within n, ob, r => withinSyn n ob r
  | .forTicks n, ob, r => forSyn n ob r
  | .afterTicks n, none, r => expandAfter r n
  | .afterTicks n, some b, r => .and (forSyn n (some b) (.not r)) (withinSyn (n + 1) (some b) r)
  | .eventually, none, r => .F r
  | .eventually, some b, r => .U (.not b) r
  | .always, none, r => .G r
  | .always, some b, r => .R b r
  | .untilStop s, none, r => .W r s
  | .untilStop s, some b, r => .or (.W r s) (.R b r)
  | .beforeStop s, none, r => .R r (.not s)
  | .beforeStop s, some b, r => .R (.or r b) (.not s)

/-- `dual_timing` -/
def dualS : STiming → STiming
  | .within n => .forTicks n
  | .forTicks n => .within n
  | .eventually => .always
  | .always => .eventually
  | .untilStop s => .beforeStop s
  | .beforeStop s => .untilStop s
  | T => T

/-- `negated_timing_body` -/
def onlySyn : STiming → Option LTL → LTL → LTL
  | .afterTicks n, ob, r => .or (withinSyn n ob r) (forSyn (n + 1) ob (.not r))
  | T, ob, r => timingSyn (dualS T) ob (.not r)

/-! ## Scopes and conditions -/

/-- `start_of_mode` -/
def somS (m : LTL) : LTL := .and (.not m) (.X m)

/-- `end_of_mode` -/
def eomS (m : LTL) : LTL := .and m (.X (.not m))

/-- `scope_boundary` -/
def boundarySyn : ScopeKind → LTL → Option LTL
  | .global, _ => none
  | .afterMode, _ => none
  | .onlyBefore, _ => none
  | .inMode, m => some (eomS m)
  | .onlyAfter, m => some (eomS m)
  | .notInMode, m => some (somS m)
  | .beforeMode, m => some (somS m)
  | .onlyIn, m => some (somS m)

/-- The body choice in `requirement_to_ltl`. -/
def bodySyn (k : ScopeKind) (T : STiming) (m r : LTL) : LTL :=
  match isOnly k with
  | true => onlySyn T (boundarySyn k m) r
  | false => timingSyn T (boundarySyn k m) r

/-- The condition handling in `requirement_to_ltl`. -/
def innerSyn : Option LTL → CondType → Bool → LTL → LTL → LTL
  | _, .trigger, true, c, x => .imp c x
  | none, .continual, _, c, x => .G (.imp c x)
  | some b, .continual, _, c, x => .R b (.imp c x)
  | none, .trigger, false, c, x => .and (.G (.imp (.and (.not c) (.X c)) (.X x))) (.imp c x)
  | some b, .trigger, false, c, x =>
    .and (.R b (.imp (.and (.not c) (.and (.X c) (.not b))) (.and (.X x) (.not b))))
      (.imp c x)

/-- `scope_wrap` -/
def wrapSyn : ScopeKind → LTL → LTL → LTL
  | .global, _, x => x
  | .onlyAfter, _, x => x
  | .inMode, m, x => .and (.G (.or (.not (somS m)) (.X x))) (.imp m x)
  | .notInMode, m, x => .and (.G (.or (.not (eomS m)) (.X x))) (.imp (.not m) x)
  | .onlyIn, m, x => .and (.G (.or (.not (eomS m)) (.X x))) (.imp (.not m) x)
  | .beforeMode, m, x => .or x m
  | .afterMode, m, x =>
    .or (.U (.not (eomS m)) (.and (eomS m) (.X x))) (.G (.not (eomS m)))
  | .onlyBefore, m, x =>
    .and (.imp (.not m) (.or (.U (.not (somS m)) (.and (somS m) (.X x))) (.G (.not (somS m)))))
      (.imp m x)

/-- `requirement_to_ltl`. The `true`-condition branch fires when the condition
is the literal `true`, as the C++ tests its printed form. -/
def lowerSyn (k : ScopeKind) (ct : CondType) (T : STiming) (m c r : LTL) : LTL :=
  wrapSyn k m (innerSyn (boundarySyn k m) ct (decide (c = LTL.tt)) c (bodySyn k T m r))

/-! ## The formulas mean what the model says -/

theorem at_eq {p : Pred} {a b : Nat} (h : a = b) (hp : p a) : p b := by
  subst h
  exact hp

theorem orIff {a b c d : Prop} (h1 : a ↔ c) (h2 : b ↔ d) : (a ∨ b) ↔ (c ∨ d) :=
  ⟨Or.imp h1.mp h2.mp, Or.imp h1.mpr h2.mpr⟩

theorem andIff {a b c d : Prop} (h1 : a ↔ c) (h2 : b ↔ d) : (a ∧ b) ↔ (c ∧ d) :=
  ⟨fun ⟨x, y⟩ => ⟨h1.mp x, h2.mp y⟩, fun ⟨x, y⟩ => ⟨h1.mpr x, h2.mpr y⟩⟩

theorem Flt_succ {n t : Nat} {p : Pred} : Flt (n + 1) p t ↔ Fle n p t :=
  ⟨fun ⟨k, hk, h⟩ => ⟨k, by omega, h⟩, fun ⟨k, hk, h⟩ => ⟨k, by omega, h⟩⟩

variable (σ : String → Pred)

theorem eval_expandWithin (r : LTL) (n t : Nat) :
    evalL σ (expandWithin r n) t ↔ Fle n (evalL σ r) t := by
  induction n generalizing t with
  | zero =>
    show evalL σ r t ↔ ∃ k, k ≤ 0 ∧ evalL σ r (t + k)
    constructor
    · intro h
      exact ⟨0, Nat.le_refl 0, h⟩
    · intro ⟨k, hk, h⟩
      have : k = 0 := by omega
      subst this
      exact h
  | succ n ih =>
    show (evalL σ r t ∨ evalL σ (expandWithin r n) (t + 1)) ↔
      ∃ k, k ≤ n + 1 ∧ evalL σ r (t + k)
    rw [ih (t + 1)]
    constructor
    · intro h
      rcases h with h | ⟨k, hk, h⟩
      · exact ⟨0, Nat.zero_le _, h⟩
      · exact ⟨k + 1, by omega, at_eq (p := evalL σ r) (by omega) h⟩
    · intro ⟨k, hk, h⟩
      cases k with
      | zero => exact Or.inl h
      | succ k => exact Or.inr ⟨k, by omega, at_eq (p := evalL σ r) (by omega) h⟩

theorem eval_expandFor (r : LTL) (n t : Nat) :
    evalL σ (expandFor r n) t ↔ Gle n (evalL σ r) t := by
  induction n generalizing t with
  | zero =>
    constructor
    · intro h k hk
      have : k = 0 := by omega
      subst this
      exact h
    · intro h
      exact h 0 (Nat.le_refl 0)
  | succ n ih =>
    show (evalL σ r t ∧ evalL σ (expandFor r n) (t + 1)) ↔ ∀ k, k ≤ n + 1 → evalL σ r (t + k)
    rw [ih (t + 1)]
    constructor
    · intro ⟨h0, h1⟩ k hk
      cases k with
      | zero => exact h0
      | succ k => exact at_eq (p := evalL σ r) (by omega) (h1 k (by omega))
    · intro h
      exact ⟨h 0 (Nat.zero_le _),
        fun k hk => at_eq (p := evalL σ r) (by omega) (h (k + 1) (by omega))⟩

theorem eval_afterChain (r : LTL) (k t : Nat) :
    evalL σ (afterChain r k) t ↔
      (∀ i, i < k → ¬ evalL σ r (t + i)) ∧ evalL σ r (t + k) := by
  induction k generalizing t with
  | zero =>
    exact ⟨fun h => ⟨fun i hi => absurd hi (Nat.not_lt_zero i), h⟩, fun h => h.2⟩
  | succ k ih =>
    show (¬ evalL σ r t ∧ evalL σ (afterChain r k) (t + 1)) ↔
      (∀ i, i < k + 1 → ¬ evalL σ r (t + i)) ∧ evalL σ r (t + (k + 1))
    rw [ih (t + 1)]
    constructor
    · intro ⟨hn, hall, hr⟩
      refine ⟨fun i hi => ?_, at_eq (p := evalL σ r) (by omega) hr⟩
      cases i with
      | zero => exact hn
      | succ i => exact at_eq (p := fun a => ¬ evalL σ r a) (by omega) (hall i (by omega))
    · intro ⟨hall, hr⟩
      exact ⟨hall 0 (by omega),
        fun i hi => at_eq (p := fun a => ¬ evalL σ r a) (by omega) (hall (i + 1) (by omega)),
        at_eq (p := evalL σ r) (by omega) hr⟩

theorem eval_expandAfter (r : LTL) (n t : Nat) :
    evalL σ (expandAfter r n) t ↔
      Gle n (neg (evalL σ r)) t ∧ evalL σ r (t + (n + 1)) :=
  (eval_afterChain σ r (n + 1) t).trans
    ⟨fun ⟨h1, h2⟩ => ⟨fun k hk => h1 k (by omega), h2⟩,
     fun ⟨h1, h2⟩ => ⟨fun i hi => h1 i (by omega), h2⟩⟩

theorem eval_withinSyn_some (n : Nat) (b r : LTL) (t : Nat) :
    evalL σ (withinSyn n (some b) r) t ↔ withinB n (evalL σ b) (evalL σ r) t := by
  cases n with
  | zero =>
    refine (eval_expandWithin σ r 0 t).trans ⟨Or.inl, ?_⟩
    intro h
    rcases h with h | ⟨_, hk, _⟩
    · exact h
    · exact absurd hk (Nat.not_lt_zero _)
  | succ n =>
    exact orIff (eval_expandWithin σ r (n + 1) t)
      ((eval_expandWithin σ b n t).trans (Flt_succ (p := evalL σ b) (t := t)).symm)

theorem eval_timingSyn_some (T : STiming) (b r : LTL) (t : Nat) :
    evalL σ (timingSyn T (some b) r) t ↔ body (T.sem σ) (evalL σ b) (evalL σ r) t := by
  cases T with
  | immediately => exact Iff.rfl
  | nextTick => exact Iff.rfl
  | within n => exact eval_withinSyn_some σ n b r t
  | forTicks n => exact orIff (eval_expandFor σ r n t) Iff.rfl
  | afterTicks n =>
    exact andIff (orIff (eval_expandFor σ (.not r) n t) Iff.rfl)
      (eval_withinSyn_some σ (n + 1) b r t)
  | eventually => exact Iff.rfl
  | always => exact Iff.rfl
  | untilStop s => exact Iff.rfl
  | beforeStop s => exact Iff.rfl

theorem eval_timingSyn_none (T : STiming) (r : LTL) (t : Nat) :
    evalL σ (timingSyn T none r) t ↔ bodyU (T.sem σ) (evalL σ r) t := by
  cases T with
  | immediately => exact Iff.rfl
  | nextTick => exact Iff.rfl
  | within n => exact eval_expandWithin σ r n t
  | forTicks n => exact eval_expandFor σ r n t
  | afterTicks n => exact eval_expandAfter σ r n t
  | eventually => exact Iff.rfl
  | always => exact Iff.rfl
  | untilStop s => exact Iff.rfl
  | beforeStop s => exact Iff.rfl

theorem eval_timingSyn (T : STiming) (ob : Option LTL) (r : LTL) (t : Nat) :
    evalL σ (timingSyn T ob r) t ↔
      body (T.sem σ) ((ob.map (evalL σ)).getD ff) (evalL σ r) t := by
  cases ob with
  | none => exact (eval_timingSyn_none σ T r t).trans (body_ff (T.sem σ) (evalL σ r) t).symm
  | some b => exact eval_timingSyn_some σ T b r t

theorem eval_onlySyn (T : STiming) (ob : Option LTL) (r : LTL) (t : Nat) :
    evalL σ (onlySyn T ob r) t ↔
      onlyBody (T.sem σ) ((ob.map (evalL σ)).getD ff) (evalL σ r) t := by
  cases T with
  | afterTicks n =>
    exact orIff (eval_timingSyn σ (.within n) ob r t)
      (eval_timingSyn σ (.forTicks (n + 1)) ob (.not r) t)
  | immediately => exact eval_timingSyn σ .immediately ob (.not r) t
  | nextTick => exact eval_timingSyn σ .nextTick ob (.not r) t
  | within n => exact eval_timingSyn σ (.forTicks n) ob (.not r) t
  | forTicks n => exact eval_timingSyn σ (.within n) ob (.not r) t
  | eventually => exact eval_timingSyn σ .always ob (.not r) t
  | always => exact eval_timingSyn σ .eventually ob (.not r) t
  | untilStop s => exact eval_timingSyn σ (.beforeStop s) ob (.not r) t
  | beforeStop s => exact eval_timingSyn σ (.untilStop s) ob (.not r) t

theorem eval_boundarySyn (k : ScopeKind) (m : LTL) :
    (boundarySyn k m).map (evalL σ) = scopeBoundary k (evalL σ m) := by
  cases k <;> rfl

theorem eval_bodySyn (k : ScopeKind) (T : STiming) (m r : LTL) :
    evalL σ (bodySyn k T m r) = timingBody k (T.sem σ) (evalL σ m) (evalL σ r) := by
  funext t
  apply propext
  cases k with
  | global => exact eval_timingSyn σ T (boundarySyn .global m) r t
  | inMode => exact eval_timingSyn σ T (boundarySyn .inMode m) r t
  | notInMode => exact eval_timingSyn σ T (boundarySyn .notInMode m) r t
  | beforeMode => exact eval_timingSyn σ T (boundarySyn .beforeMode m) r t
  | afterMode => exact eval_timingSyn σ T (boundarySyn .afterMode m) r t
  | onlyIn => exact eval_onlySyn σ T (boundarySyn .onlyIn m) r t
  | onlyBefore => exact eval_onlySyn σ T (boundarySyn .onlyBefore m) r t
  | onlyAfter => exact eval_onlySyn σ T (boundarySyn .onlyAfter m) r t

theorem eval_innerSyn (ob : Option LTL) (ct : CondType) (ctrue : Bool) (c x : LTL) :
    evalL σ (innerSyn ob ct ctrue c x) =
      inner (ob.map (evalL σ)) ct ctrue (evalL σ c) (evalL σ x) := by
  cases ob <;> cases ct <;> cases ctrue <;> rfl

theorem eval_wrapSyn (k : ScopeKind) (m x : LTL) :
    evalL σ (wrapSyn k m x) = wrap k (evalL σ m) (evalL σ x) := by
  cases k <;> rfl

/-- The formula `lowerSyn` builds means `lower`. -/
theorem eval_lowerSyn (k : ScopeKind) (ct : CondType) (T : STiming) (m c r : LTL) :
    evalL σ (lowerSyn k ct T m c r) =
      lower k ct (decide (c = LTL.tt)) (T.sem σ) (evalL σ m) (evalL σ c) (evalL σ r) := by
  unfold lowerSyn lower
  rw [eval_wrapSyn, eval_innerSyn, eval_boundarySyn, eval_bodySyn]

end Fretish
