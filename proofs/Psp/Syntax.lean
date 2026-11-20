import Psp.Scopes
import Fretish.Syntax

/-!
# The PSP lowering as syntax

`lowerSyn` builds an LTL formula the way `requirement_to_ltl` in
`src/psp/lowering.cpp` builds its string: each `*Syn` function below follows
one C++ function, branch for branch. The bodies with no boundary are the C++
`unbounded_body`, written with `G` and `F`, not the bounded body at
`b = false`. The formula type `LTL`, its semantics `evalL` and its printer
`LTL.render` are those of `Fretish.Syntax`.

`eval_lowerSyn` proves that the formula's meaning is `lower`, so every theorem
about `lower` holds of the formula. `PspLowering.lean` prints it for every
pattern, scope and count from 1 to 4, for `scripts/check_lean_psp_lowering.py`
to compare with the C++ output.
-/

namespace Psp

open Fretish (LTL evalL Pred)

variable {σ : String → Pred}

/-- `next`: `X^n φ`. -/
def nextSyn : Nat → LTL → LTL
  | 0, φ => φ
  | n + 1, φ => .X (nextSyn n φ)

/-- `conj`: the parts joined by `&`. -/
def conjL : List LTL → LTL
  | [] => .tt
  | [φ] => φ
  | φ :: l => .and φ (conjL l)

/-- `disj`: the parts joined by `|`. -/
def disjL : List LTL → LTL
  | [] => .not .tt
  | [φ] => φ
  | φ :: l => .or φ (disjL l)

/-- `within`: `F[0,n] φ` as an X chain. -/
def withinSyn (n : Nat) (φ : LTL) : LTL := disjL ((List.range (n + 1)).map (nextSyn · φ))

/-- `throughout`: `G[0,n] φ` as an X chain. -/
def throughoutSyn (n : Nat) (φ : LTL) : LTL := conjL ((List.range (n + 1)).map (nextSyn · φ))

/-- `within_before` -/
def withinBeforeSyn (n : Nat) (s b : LTL) : LTL :=
  disjL ((List.range (n + 1)).map fun j =>
    conjL (nextSyn j (.and s (.not b)) :: (List.range j).map (nextSyn · (.not b))))

/-- `held_until` -/
def heldUntilSyn (n : Nat) (s b : LTL) : LTL :=
  conjL ((List.range (n + 1)).map fun j =>
    disjL (nextSyn j s :: (List.range (j + 1)).map (nextSyn · b)))

/-- `at_most` -/
def atMostSyn (p : LTL) : Option LTL → Nat → LTL
  | none, 0 => .G (.not p)
  | none, k + 1 => .W (.not p) (.W p (atMostSyn p none k))
  | some b, 0 => .W (.not p) b
  | some b, k + 1 => .W (.not p) (.or b (.W p (.or b (atMostSyn p (some b) k))))

/-- `unbounded_body` -/
def unboundedSyn (a : Pat) (p s : LTL) : LTL :=
  match a with
  | .absence => .G (.not p)
  | .universality => .G s
  | .existence => .F s
  | .precedence => .W (.not p) s
  | .response => .G (.imp p (.F s))
  | .bresp n => .G (.imp p (withinSyn n s))
  | .binv n => .G (.imp p (throughoutSyn n s))
  | .brec n => .G (withinSyn n s)
  | .bexist k => atMostSyn p none k

/-- `bounded_body` -/
def boundedSyn (a : Pat) (p s b : LTL) : LTL :=
  match a with
  | .absence => .W (.not p) b
  | .universality => .W s b
  | .existence => .U (.not b) (.and s (.not b))
  | .precedence => .W (.not p) (.or s b)
  | .response => .W (.imp p (.U (.not b) (.and s (.not b)))) b
  | .bresp n => .W (.imp p (withinBeforeSyn n s b)) b
  | .binv n => .W (.imp p (heldUntilSyn n s b)) b
  | .brec n => .W (withinSyn n (.or s b)) b
  | .bexist k => atMostSyn p (some b) k

/-- `requirement_to_ltl` for a pattern requirement. -/
def lowerSyn (k : Scope) (a : Pat) (p s q r : LTL) : LTL :=
  match k with
  | .globally => unboundedSyn a p s
  | .before => .imp (.F r) (boundedSyn a p s r)
  | .after => .W (.not q) (.and q (unboundedSyn a p s))
  | .between => .G (.imp (.and (.and q (.not r)) (.F r)) (boundedSyn a p s r))
  | .afterUntil => .G (.imp (.and q (.not r)) (boundedSyn a p s r))

/-! ## Meaning -/

theorem eval_nextSyn (φ : LTL) : ∀ n t, evalL σ (nextSyn n φ) t ↔ evalL σ φ (t + n)
  | 0, t => Iff.rfl
  | n + 1, t => by
    simp only [nextSyn, evalL]
    rw [eval_nextSyn φ n (t + 1), show t + 1 + n = t + (n + 1) by omega]

theorem eval_conjL : ∀ (l : List LTL) t, evalL σ (conjL l) t ↔ ∀ φ ∈ l, evalL σ φ t
  | [], t => by simp [conjL, evalL]
  | [φ], t => by simp [conjL]
  | φ :: ψ :: l, t => by
    simp only [conjL, evalL, eval_conjL (ψ :: l) t]
    simp

theorem eval_disjL : ∀ (l : List LTL) t, evalL σ (disjL l) t ↔ ∃ φ ∈ l, evalL σ φ t
  | [], t => by simp [disjL, evalL]
  | [φ], t => by simp [disjL]
  | φ :: ψ :: l, t => by
    simp only [disjL, evalL, eval_disjL (ψ :: l) t]
    simp

theorem eval_withinSyn (n : Nat) (φ : LTL) (t : Nat) :
    evalL σ (withinSyn n φ) t ↔ ∃ k, k ≤ n ∧ evalL σ φ (t + k) := by
  simp only [withinSyn, eval_disjL, List.mem_map, List.mem_range]
  constructor
  · rintro ⟨_, ⟨k, hk, rfl⟩, h⟩
    exact ⟨k, by omega, (eval_nextSyn φ k t).mp h⟩
  · rintro ⟨k, hk, h⟩
    exact ⟨_, ⟨k, by omega, rfl⟩, (eval_nextSyn φ k t).mpr h⟩

theorem eval_throughoutSyn (n : Nat) (φ : LTL) (t : Nat) :
    evalL σ (throughoutSyn n φ) t ↔ ∀ k, k ≤ n → evalL σ φ (t + k) := by
  simp only [throughoutSyn, eval_conjL, List.mem_map, List.mem_range]
  constructor
  · intro h k hk
    exact (eval_nextSyn φ k t).mp (h _ ⟨k, by omega, rfl⟩)
  · rintro h _ ⟨k, hk, rfl⟩
    exact (eval_nextSyn φ k t).mpr (h k (by omega))

theorem ex_map {α β : Type} {p : α → Prop} {f : α → β} {q : β → Prop} :
    (∃ y, (∃ x, p x ∧ f x = y) ∧ q y) ↔ ∃ x, p x ∧ q (f x) :=
  ⟨fun ⟨_, ⟨x, hx, he⟩, hq⟩ => ⟨x, hx, he ▸ hq⟩, fun ⟨x, hx, hq⟩ => ⟨_, ⟨x, hx, rfl⟩, hq⟩⟩

theorem eval_withinBeforeSyn (n : Nat) (s b : LTL) (t : Nat) :
    evalL σ (withinBeforeSyn n s b) t ↔ withinB n (evalL σ b) (evalL σ s) t := by
  simp [withinBeforeSyn, eval_disjL, eval_conjL, eval_nextSyn, evalL, ex_map, withinB,
    Nat.lt_add_one_iff]

theorem eval_heldUntilSyn (n : Nat) (s b : LTL) (t : Nat) :
    evalL σ (heldUntilSyn n s b) t ↔ heldB n (evalL σ b) (evalL σ s) t := by
  simp [heldUntilSyn, eval_disjL, eval_conjL, eval_nextSyn, ex_map, heldB, Nat.lt_add_one_iff]

theorem eval_atMostSyn_none (p : LTL) :
    ∀ k t, evalL σ (atMostSyn p none k) t ↔ atMost Fretish.ff (evalL σ p) k t
  | 0, t => by simp [atMostSyn, atMost, evalL]
  | k + 1, t => by
    simp only [atMostSyn, atMost, evalL, eval_atMostSyn_none p k]
    simp

theorem eval_atMostSyn_some (p b : LTL) :
    ∀ k t, evalL σ (atMostSyn p (some b) k) t ↔ atMost (evalL σ b) (evalL σ p) k t
  | 0, t => Iff.rfl
  | k + 1, t => by
    simp only [atMostSyn, atMost, evalL, eval_atMostSyn_some p b k]

theorem eval_unboundedSyn (a : Pat) (p s : LTL) (t : Nat) :
    evalL σ (unboundedSyn a p s) t ↔ body a Fretish.ff (evalL σ p) (evalL σ s) t := by
  cases a <;>
    simp [unboundedSyn, body, evalL, eval_withinSyn, eval_throughoutSyn, eval_atMostSyn_none,
      withinB, heldB, Fretish.W, Fretish.U, Fretish.G, Fretish.neg, Fretish.ff]

theorem eval_boundedSyn (a : Pat) (p s b : LTL) (t : Nat) :
    evalL σ (boundedSyn a p s b) t ↔ body a (evalL σ b) (evalL σ p) (evalL σ s) t := by
  cases a <;>
    simp [boundedSyn, body, evalL, eval_withinSyn, eval_withinBeforeSyn, eval_heldUntilSyn,
      eval_atMostSyn_some, Fretish.W, Fretish.U, Fretish.G, Fretish.neg]

/-- The C++ formula means the model, at every pattern, scope and count. -/
theorem eval_lowerSyn (k : Scope) (a : Pat) (p s q r : LTL) (t : Nat) :
    evalL σ (lowerSyn k a p s q r) t ↔
      lower k a (evalL σ p) (evalL σ s) (evalL σ q) (evalL σ r) t := by
  cases k <;> simp [lowerSyn, lower, evalL, eval_unboundedSyn, eval_boundedSyn, and_assoc,
    Fretish.W, Fretish.U, Fretish.G, Fretish.F, Fretish.neg]

end Psp
