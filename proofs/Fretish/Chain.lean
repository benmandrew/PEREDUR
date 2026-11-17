import Fretish.CondOrder

/-!
# The chain search

`Search.reach` transcribes `RuleSearch` in `src/filter/implication_check.cpp`,
which `requirement_implies` runs when two lowerings differ. A state is the set
of fields already moved from `src`'s value to `dst`'s, and a step moves one
more field if `stepHolds` accepts it in the context of the current state.

The propositional checks are not transcribed. `formula_implies` decides them
by model counting, and here they are six answers that `Search.Sound` assumes
correct. Each scope's mode is reduced to `sameMode`, as in `scopeImplies`.

`RuleSearch` memoises failed states. Each step moves a new field, so a search
of depth five reaches every state, and the memo can only cut a branch that
already failed. `reach` leaves it out.

`reach_sound` proves that every accepted pair implies: each step is one of the
field theorems at the state's other fields, and implication is transitive.
-/

namespace Fretish

inductive Field where
  | condition
  | response
  | timing
  | conditionType
  | scope
  deriving DecidableEq

def Field.all : List Field := [.condition, .response, .timing, .conditionType, .scope]

/-- The fields already moved to `dst`'s value. -/
abbrev Moved := Field → Bool

def Moved.set (mv : Moved) (f : Field) : Moved := fun g => if g = f then true else mv g

/-- A requirement. A timing is a kind and a stop, which a stop-free kind
ignores, and `ctrue` marks the condition printed as the literal `true`. -/
structure Req where
  cond : Pred
  ctrue : Bool
  resp : Pred
  kind : Kind
  stop : Pred
  ct : CondType
  scope : ScopeKind
  mode : Pred

def Req.sem (q : Req) : Pred := lower q.scope q.ct q.ctrue (q.kind.at q.stop) q.mode q.cond q.resp

/-- What the C++ guarantees of a requirement: no `within 0` or `for 0`, and
`ctrue` only on a condition that is `true`. -/
def Req.Valid (q : Req) : Prop := q.kind.cppValid ∧ (q.ctrue = false ∨ q.cond = tt)

/-- `RuleSearch::at`, field by field. -/
def mix (mv : Moved) (r r' : Req) : Req where
  cond := if mv .condition then r'.cond else r.cond
  ctrue := if mv .condition then r'.ctrue else r.ctrue
  resp := if mv .response then r'.resp else r.resp
  kind := if mv .timing then r'.kind else r.kind
  stop := if mv .timing then r'.stop else r.stop
  ct := if mv .conditionType then r'.ct else r.ct
  scope := if mv .scope then r'.scope else r.scope
  mode := if mv .scope then r'.mode else r.mode

/-- A search, with `formula_implies`'s answers for each propositional field:
`fwd` for `src`'s value implying `dst`'s, `back` for the converse. -/
structure Search where
  src : Req
  dst : Req
  sameMode : Bool
  condFwd : Bool
  condBack : Bool
  respFwd : Bool
  respBack : Bool
  stopFwd : Bool
  stopBack : Bool

/-- The answers are correct, and both requirements valid. -/
structure Search.Sound (S : Search) : Prop where
  src : S.src.Valid
  dst : S.dst.Valid
  mode : S.sameMode = true → S.dst.mode = S.src.mode
  condFwd : S.condFwd = true → Entails S.src.cond S.dst.cond
  condBack : S.condBack = true → Entails S.dst.cond S.src.cond
  respFwd : S.respFwd = true → Entails S.src.resp S.dst.resp
  respBack : S.respBack = true → Entails S.dst.resp S.src.resp
  stopFwd : S.stopFwd = true → Entails S.src.stop S.dst.stop
  stopBack : S.stopBack = true → Entails S.dst.stop S.src.stop

/-! ## The transcription -/

def Kind.isAfter : Kind → Bool
  | .basic (.after _) => true
  | _ => false

def Kind.isBefore : Kind → Bool
  | .beforeK => true
  | _ => false

abbrev Search.state (S : Search) (mv : Moved) : Req := mix mv S.src S.dst

/-- `timing_step`: along the stop's polarity if only the stop changes, else
along `timing_implies`. -/
def Search.timingStep (S : Search) (k : ScopeKind) : Bool :=
  if S.src.kind.carriesStop && S.dst.kind.carriesStop && S.src.kind == S.dst.kind then
    if S.src.kind.isBefore != isOnly k then S.stopBack else S.stopFwd
  else timingImplies S.src.kind S.dst.kind k

/-- `step_holds` -/
def Search.stepHolds (S : Search) (mv : Moved) : Field → Bool
  | .condition =>
    if (S.state mv).ct == .continual then S.condBack else S.condFwd && S.condBack
  | .response =>
    if (S.state mv).kind.isAfter then S.respFwd && S.respBack
    else if isOnly (S.state mv).scope then S.respBack else S.respFwd
  | .timing => S.timingStep (S.state mv).scope
  | .conditionType => conditionTypeStep S.src.ct (S.state mv).scope (S.state mv).kind
  | .scope => scopeImplies S.src.scope S.dst.scope S.sameMode (S.state mv).kind (S.state mv).ct

/-- `reach`, to depth `n`. -/
def Search.reachN (S : Search) : Nat → Moved → Bool
  | 0, mv => Field.all.all mv
  | n + 1, mv =>
    Field.all.all mv ||
      Field.all.any (fun f => !mv f && S.stepHolds mv f && S.reachN n (mv.set f))

/-- `reaches_dest`, from the fields `src` and `dst` already share. -/
def Search.reach (S : Search) (start : Moved) : Bool := S.reachN 5 start

/-! ## Soundness -/

variable {S : Search} {mv : Moved}

theorem eq_of_entails {p q : Pred} (h : Entails p q) (h' : Entails q p) : p = q :=
  funext fun t => propext ⟨h t, h' t⟩

theorem Kind.at_stop {K : Kind} (h : K.carriesStop = false) (s s' : Pred) : K.at s = K.at s' := by
  rcases K with _ | _ | _ <;> simp_all [Kind.carriesStop, Kind.at]

theorem Kind.at_not_after {K : Kind} (h : K.isAfter = false) (s : Pred) :
    ∀ n, K.at s ≠ .afterTicks n := by
  rcases K with ⟨A⟩ | _ | _ <;> (try cases A) <;> simp_all [Kind.isAfter, Kind.at, Basic.timing]

/-- A continual requirement ignores `ctrue`. -/
theorem inner_continual (ob : Option Pred) (a b : Bool) (c x : Pred) :
    inner ob .continual a c x = inner ob .continual b c x := by
  cases ob <;> cases a <;> cases b <;> rfl

/-- A trigger on a condition that always holds is its body, whatever `ctrue` says. -/
theorem inner_trigger_tt (ob : Option Pred) (a : Bool) (x : Pred) (t : Nat) :
    inner ob .trigger a tt x t ↔ x t := by
  cases ob <;> cases a
  · exact ⟨fun h => h.2 trivial, fun h => ⟨fun _ ⟨hn, _⟩ => absurd trivial hn, fun _ => h⟩⟩
  · exact ⟨fun h => h trivial, fun h _ => h⟩
  · exact ⟨fun h => h.2 trivial, fun h => ⟨fun _ => Or.inl (fun ⟨hn, _⟩ => absurd trivial hn),
      fun _ => h⟩⟩
  · exact ⟨fun h => h trivial, fun h _ => h⟩

/-- `timingOrder` on whole requirements. -/
theorem lower_timingOrder {k : ScopeKind} {K K' : Kind} (h : timingOrder k K K') (s : Pred)
    {ct : CondType} {ctrue : Bool} {m c r : Pred} :
    Entails (lower k ct ctrue (K.at s) m c r) (lower k ct ctrue (K'.at s) m c r) := by
  cases k
  · exact lower_unbounded _ (.inl rfl) (h s)
  · exact lower_bounded _ (.inl rfl) (h s)
  · exact lower_bounded _ (.inr (.inl rfl)) (h s)
  · exact lower_bounded _ (.inr (.inr rfl)) (h s)
  · exact lower_unbounded _ (.inr rfl) (h s)
  · exact lower_only_bounded _ (.inl rfl) (h s)
  · exact lower_only_unbounded (h s)
  · exact lower_only_bounded _ (.inr rfl) (h s)

/-- Under `timing_implies`, two distinct kinds never both carry a stop. -/
theorem timingImplies_stop {K K' : Kind} {k : ScopeKind}
    (hne : ¬ (K.carriesStop && K'.carriesStop && K == K') = true)
    (h : timingImplies K K' k = true) : K.carriesStop = false ∨ K'.carriesStop = false := by
  rcases K with ⟨A⟩ | _ | _ <;> rcases K' with ⟨B⟩ | _ | _ <;>
    cases k <;> simp_all [Kind.carriesStop, timingImplies, globalTimingImplies]

/-- A timing step at the state's scope. -/
theorem timing_sound (hS : S.Sound) (h : S.timingStep k = true) {ct : CondType} {ctrue : Bool}
    {m c r : Pred} :
    Entails (lower k ct ctrue (S.src.kind.at S.src.stop) m c r)
      (lower k ct ctrue (S.dst.kind.at S.dst.stop) m c r) := by
  unfold Search.timingStep at h
  split at h
  · rename_i hso
    simp only [Bool.and_eq_true, beq_iff_eq] at hso
    obtain ⟨⟨hc, -⟩, he⟩ := hso
    rw [← he]
    revert h hc
    generalize S.src.kind = K
    intro h hc
    rcases K with _ | _ | _ <;> simp [Kind.carriesStop] at hc <;>
      cases k <;> simp [Kind.isBefore, isOnly] at h <;> simp only [Kind.at] <;>
      first
      | exact lower_mono _ (until_stop_mono (hS.stopFwd h))
      | exact lower_mono _ (before_stop_anti (hS.stopBack h))
      | exact lower_mono _ (only_until_stop_anti (hS.stopBack h))
      | exact lower_mono _ (only_before_stop_mono (hS.stopFwd h))
  · rename_i hso
    have ho := timing_implies_sound k hS.src.1 hS.dst.1 h
    rcases timingImplies_stop hso h with hs | hs
    · rw [Kind.at_stop hs S.src.stop S.dst.stop]
      exact lower_timingOrder ho _
    · rw [Kind.at_stop hs S.dst.stop S.src.stop]
      exact lower_timingOrder ho _

/-! ## Each step, at any context -/

theorem lower_trigger_tt {k : ScopeKind} {T : Timing} {m r : Pred} (a b : Bool) :
    Entails (lower k .trigger a T m tt r) (lower k .trigger b T m tt r) :=
  wrap_mono k m (fun t h => (inner_trigger_tt _ b _ t).mpr ((inner_trigger_tt _ a _ t).mp h))

theorem condition_sound (hS : S.Sound) {k : ScopeKind} {ct : CondType} {T : Timing} {m r : Pred}
    (h : (if (ct == .continual) = true then S.condBack else S.condFwd && S.condBack) = true) :
    Entails (lower k ct S.src.ctrue T m S.src.cond r) (lower k ct S.dst.ctrue T m S.dst.cond r) := by
  cases ct
  · unfold lower
    rw [inner_continual _ S.dst.ctrue S.src.ctrue]
    exact wrap_mono k m (inner_condition_anti _ _ (hS.condBack (by simpa using h)))
  · have h : S.condFwd = true ∧ S.condBack = true := by simpa using h
    have he := eq_of_entails (hS.condFwd h.1) (hS.condBack h.2)
    rw [← he]
    cases ha : S.src.ctrue <;> cases hb : S.dst.ctrue
    · exact fun _ => id
    · rw [he, (hS.dst.2.resolve_left (by simp [hb]))]
      exact lower_trigger_tt _ _
    · rw [(hS.src.2.resolve_left (by simp [ha]))]
      exact lower_trigger_tt _ _
    · exact fun _ => id

theorem response_sound (hS : S.Sound) {k : ScopeKind} {ct : CondType} {ctrue : Bool} {K : Kind}
    {s m c : Pred}
    (h : (if K.isAfter = true then S.respFwd && S.respBack
      else if isOnly k = true then S.respBack else S.respFwd) = true) :
    Entails (lower k ct ctrue (K.at s) m c S.src.resp) (lower k ct ctrue (K.at s) m c S.dst.resp) := by
  split at h
  · simp only [Bool.and_eq_true] at h
    rw [eq_of_entails (hS.respFwd h.1) (hS.respBack h.2)]
    exact fun _ => id
  · rename_i ha
    have hT := Kind.at_not_after (Bool.eq_false_iff.mpr ha) s
    refine wrap_mono k m (inner_mono _ ct ctrue c ?_)
    split at h <;> rename_i hk <;> simp only [timingBody, hk, Bool.not_eq_true] at hk ⊢
    · exact only_response_anti _ hT (hS.respBack h)
    · exact body_response_mono _ hT (hS.respFwd h)

/-! ## The search -/

theorem state_valid (hS : S.Sound) (mv : Moved) : (S.state mv).Valid := by
  simp only [Search.state, mix, Req.Valid]
  cases h1 : mv .condition <;> cases h2 : mv .timing <;>
    simp [hS.src.1, hS.dst.1, hS.src.2, hS.dst.2]

theorem step_sound (hS : S.Sound) (f : Field) (hf : mv f = false) (h : S.stepHolds mv f = true) :
    Entails (S.state mv).sem (S.state (mv.set f)).sem := by
  have hc := (state_valid hS mv).2
  cases f <;>
    simp only [Search.stepHolds, Search.state, mix, Req.sem, Moved.set, hf, reduceCtorEq,
      ↓reduceIte, Bool.false_eq_true] at h hc ⊢
  · exact condition_sound hS h
  · exact response_sound hS h
  · exact timing_sound hS h
  · by_cases he : S.src.ct = S.dst.ct
    · rw [he]; exact fun _ => id
    · exact condition_type_step_sound hc he h
  · exact scope_implies_sound hc hS.mode h

theorem state_all (h : Field.all.all mv = true) : S.state mv = S.dst := by
  simp only [Field.all, List.all_cons, List.all_nil, Bool.and_true, Bool.and_eq_true] at h
  simp [Search.state, mix, h]

theorem reachN_sound (hS : S.Sound) : ∀ n mv, S.reachN n mv = true →
    Entails (S.state mv).sem S.dst.sem
  | 0, mv, h => by rw [state_all h]; exact fun _ => id
  | n + 1, mv, h => by
    simp only [Search.reachN, Bool.or_eq_true, List.any_eq_true, Bool.and_eq_true,
      Bool.not_eq_true'] at h
    rcases h with h | ⟨f, -, ⟨hf, hs⟩, hr⟩
    · rw [state_all h]; exact fun _ => id
    · exact fun t ht => reachN_sound hS n _ hr t (step_sound hS f hf hs t ht)

/-- Every pair `reach` accepts implies, from a start that marks only fields
the two requirements share. -/
theorem reach_sound (hS : S.Sound) {start : Moved} (hstart : S.state start = S.src)
    (h : S.reach start = true) : Entails S.src.sem S.dst.sem :=
  hstart ▸ reachN_sound hS 5 start h

end Fretish
