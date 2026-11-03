import Fretish.Timing

/-!
# The timing order under the only scopes

An only scope carries `negated_timing_body`: the timing's dual over the
negated response, with `after n` handled on its own. `onlyBody` follows the
lowering, including FRET's pairing of `until` with `before`, which is not the
logical complement.

Each covering edge of the only-scope orders is an edge of `impliesB` or
`implies0` read at `neg p` between the two duals, so the order reverses. The
exceptions are the edges into `after n`. Only in and only after have a
boundary and use `onlyImpliesB`; only before has none and uses
`onlyImplies0`, which adds `for n ⇒ always`.
-/

namespace Fretish

def dual : Timing → Timing
  | .within n => .forTicks n
  | .forTicks n => .within n
  | .eventually => .always
  | .always => .eventually
  | .untilStop s => .beforeStop s
  | .beforeStop s => .untilStop s
  | T => T

def onlyBody : Timing → Pred → Pred → Pred
  | .afterTicks n, b, p => fun t => withinB n b p t ∨ forB (n + 1) b (neg p) t
  | T, b, p => body (dual T) b (neg p)

abbrev onlyImpliesB (T T' : Timing) : Prop :=
  ∀ b p : Pred, Entails (onlyBody T b p) (onlyBody T' b p)

abbrev onlyImplies0 (T T' : Timing) : Prop :=
  ∀ p : Pred, Entails (onlyBody T ff p) (onlyBody T' ff p)

variable {n m : Nat} {s : Pred} {T T' T'' : Timing}

/-! ## Covering edges under a boundary -/

theorem only_within_within (h : m ≤ n) : onlyImpliesB (.within n) (.within m) :=
  fun b p t hp => for_for h b (neg p) t hp

theorem only_for_for (h : n ≤ m) : onlyImpliesB (.forTicks n) (.forTicks m) :=
  fun b p t hp => within_within h b (neg p) t hp

theorem only_within_imm : onlyImpliesB (.within n) .immediately :=
  fun b p t hp => for_imm (n := n) b (neg p) t hp

theorem only_within_next (h : 1 ≤ n) : onlyImpliesB (.within n) .nextTick :=
  fun b p t hp => for_next h b (neg p) t hp

theorem only_imm_for : onlyImpliesB .immediately (.forTicks m) :=
  fun b p t hp => imm_within (m := m) b (neg p) t hp

theorem only_next_for (h : 1 ≤ m) : onlyImpliesB .nextTick (.forTicks m) :=
  fun b p t hp => next_within h b (neg p) t hp

theorem only_eventually_within : onlyImpliesB .eventually (.within n) :=
  fun b p t hp => always_for (n := n) b (neg p) t hp

theorem only_eventually_before : onlyImpliesB .eventually (.beforeStop s) :=
  fun b p t hp => always_until (s := s) b (neg p) t hp

theorem only_imm_always : onlyImpliesB .immediately .always :=
  fun b p t hp => imm_eventually b (neg p) t hp

theorem only_within_after (h : n < m) : onlyImpliesB (.within m) (.afterTicks n) := by
  intro b p t hp
  change forB m b (neg p) t at hp
  change withinB n b p t ∨ forB (n + 1) b (neg p) t
  exact Or.inr (forB_mono (by omega) hp)

theorem only_eventually_after : onlyImpliesB .eventually (.afterTicks n) := by
  intro b p t hp
  change R b (neg p) t at hp
  change withinB n b p t ∨ forB (n + 1) b (neg p) t
  exact Or.inr (Or.inr hp)

theorem onlyImpliesB_trans (h1 : onlyImpliesB T T') (h2 : onlyImpliesB T' T'') :
    onlyImpliesB T T'' :=
  fun b p t h => h2 b p t (h1 b p t h)

/-! ## Covering edges without a boundary -/

theorem onlyImplies0_of_onlyImpliesB (h : onlyImpliesB T T') : onlyImplies0 T T' :=
  fun p => h ff p

theorem only_for_always0 : onlyImplies0 (.forTicks n) .always :=
  fun p t hp => within_eventually0 (n := n) (neg p) t hp

theorem onlyImplies0_trans (h1 : onlyImplies0 T T') (h2 : onlyImplies0 T' T'') :
    onlyImplies0 T T'' :=
  fun p t h => h2 p t (h1 p t h)

/-! ## Polarity

The response is antitone under an only scope, and the two stops swap
polarity. -/

theorem only_response_anti (T : Timing) (hT : ∀ n, T ≠ .afterTicks n) {b p q : Pred}
    (hpq : Entails p q) : Entails (onlyBody T b q) (onlyBody T b p) := by
  have hn : Entails (neg q) (neg p) := fun t hq hp => hq (hpq t hp)
  have ha : ∀ T' : Timing, (∀ n, dual T' ≠ .afterTicks n) →
      Entails (body (dual T') b (neg q)) (body (dual T') b (neg p)) :=
    fun T' h => body_response_mono (dual T') h hn
  cases T with
  | afterTicks n => exact (hT n rfl).elim
  | immediately => exact ha .immediately (fun _ h => Timing.noConfusion h)
  | nextTick => exact ha .nextTick (fun _ h => Timing.noConfusion h)
  | within k => exact ha (.within k) (fun _ h => Timing.noConfusion h)
  | forTicks k => exact ha (.forTicks k) (fun _ h => Timing.noConfusion h)
  | eventually => exact ha .eventually (fun _ h => Timing.noConfusion h)
  | always => exact ha .always (fun _ h => Timing.noConfusion h)
  | untilStop s => exact ha (.untilStop s) (fun _ h => Timing.noConfusion h)
  | beforeStop s => exact ha (.beforeStop s) (fun _ h => Timing.noConfusion h)

theorem only_until_stop_anti {b p s s' : Pred} (hs : Entails s s') :
    Entails (onlyBody (.untilStop s') b p) (onlyBody (.untilStop s) b p) :=
  before_stop_anti (b := b) (p := neg p) hs

theorem only_before_stop_mono {b p s s' : Pred} (hs : Entails s s') :
    Entails (onlyBody (.beforeStop s) b p) (onlyBody (.beforeStop s') b p) :=
  until_stop_mono (b := b) (p := neg p) hs

end Fretish
