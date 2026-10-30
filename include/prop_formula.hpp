#pragma once

/// @file prop_formula.hpp
/// @brief Propositional formula AST with parsing, simplification, DIMACS
///        conversion, and syntactic similarity.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace prop_formula_internal {
struct Node;
}  // namespace prop_formula_internal

/// A propositional formula represented as a parse tree. Supports standard
/// boolean operators (¬, ∧, ∨, →, ↔) and variable atoms. Formulae can be
/// converted to DIMACS CNF format for SAT/model counting, and their syntactic
/// structure can be analyzed for similarity metrics in repair algorithms.
///
/// Uses the PImpl pattern to hide implementation details (internal AST node
/// representation) from the public interface.
class Formula {
   public:
    /// Node kind. The trailing six are temporal operators (LTL), only produced
    /// by the temporal construction path (make_unary/make_binary with a
    /// temporal Kind) and the TLSF front end; the propositional parser never
    /// emits them, so FRETISH formulae stay propositional. New values are
    /// appended so the NodeType/Kind ordinals of the propositional kinds — and
    /// therefore every existing formula hash and ordering — are unchanged.
    /// Also the type of each node in a formula's arena. The temporal kinds are
    /// appended after the propositional ones, whose ordinals formula hashes
    /// and orderings depend on, so a new kind goes at the end.
    enum class Kind : std::uint8_t {
        Atom,
        Not,
        And,
        Or,
        Implies,
        Iff,
        Next,        ///< X phi
        Eventually,  ///< F phi
        Globally,    ///< G phi
        Until,       ///< phi U psi
        Release,     ///< phi R psi
        WeakUntil,   ///< phi W psi
    };

    using RewriteCallback =
        std::function<std::optional<Formula>(const Formula&)>;

    /// Default constructor creates a formula representing the logical constant
    /// "true" (implemented as a single atom named "true"). The parser gives
    /// "true" and "false" no special treatment — both parse as ordinary atoms.
    /// They are reserved by convention and recognised downstream instead; see
    /// is_constant_atom in src/requirement.cpp.
    Formula();

    /// Static instance of the logical constant "true" for convenience.
    static Formula true_formula;

    /// Constructs a Formula by parsing a string representation.
    /// Supports operators: ! or ~ (negation), & (and), | (or), -> (implies),
    /// <-> (iff). Variables are alphanumeric identifiers (including _).
    /// @param formula A string representation of the propositional formula
    /// @throws std::invalid_argument if the formula is malformed or empty
    explicit Formula(const std::string& formula);

    /// Parses @p formula, reporting a string it cannot read rather than
    /// asserting on one. The constructor above asserts, which is right for
    /// every caller that built its own input; this is for the ones handed a
    /// string from outside -- a cache key derived from a tool's output, say --
    /// where a spelling this parser does not accept is a missed collapse
    /// rather than a defect. Under NDEBUG the asserts are gone and the walk
    /// used to run past the end of the text, so this is also the only safe
    /// entry point for an untrusted string in a release build.
    [[nodiscard]] static std::optional<Formula> try_parse(
        const std::string& formula);

    Formula(const Formula& other);
    Formula(Formula&& other) noexcept;
    Formula& operator=(const Formula& other);
    Formula& operator=(Formula&& other) noexcept;
    ~Formula();

    /// Creates an atomic formula from an identifier.
    /// @param atom The atom name
    /// @return     A formula containing a single atom
    static Formula make_atom(const std::string& atom);

    /// Creates a unary formula.
    /// @param kind  Unary operator kind: Kind::Not, or a temporal unary kind
    ///              (Kind::Next, Kind::Eventually, Kind::Globally)
    /// @param child Operand formula
    /// @return      A formula of the form op(child)
    static Formula make_unary(Kind kind, const Formula& child);

    /// Creates a binary formula.
    /// @param kind  Binary operator kind
    /// @param left  Left operand
    /// @param right Right operand
    /// @return      A formula of the form left op right
    static Formula make_binary(Kind kind, const Formula& left,
                               const Formula& right);

    /// Removes all double negations (!!A → A) from this formula in-place.
    void remove_double_negation();

    /// Simplifies this formula in-place using boolean identities:
    /// idempotence (A∧A→A, A∨A→A), tautology (A→A→true, A↔A→true),
    /// excluded middle (A∨¬A→true), contradiction (A∧¬A→false, A↔¬A→false),
    /// identity/absorption with the true and false constants, and ¬¬A→A.
    void simplify();

    /// Returns the kind of this formula's root node.
    [[nodiscard]] Kind kind() const;

    /// Returns this formula's atom name if it is atomic.
    /// @return std::nullopt for non-atomic formulae
    [[nodiscard]] std::optional<std::string> atom_name() const;

    /// Returns this formula's unary child if its root is unary.
    /// @return std::nullopt for non-unary formulae
    [[nodiscard]] std::optional<Formula> unary_child() const;

    /// Returns this formula's binary children if its root is binary.
    /// @return std::nullopt for non-binary formulae
    [[nodiscard]] std::optional<std::pair<Formula, Formula>> binary_children()
        const;

    /// Rewrites this formula using a post-order callback.
    /// Children are rewritten before their parent; if the callback returns a
    /// replacement, that replacement is used for the current subtree.
    /// @param rewrite_callback Callback that can replace a subtree
    /// @return                 The rewritten formula
    [[nodiscard]] Formula rewrite_post_order(
        const RewriteCallback& rewrite_callback) const;

    /// Converts the formula to DIMACS CNF format for use with SAT/model
    /// counters. Uses Tseitin encoding to transform the formula into CNF.
    /// @return A string in DIMACS format (p cnf &lt;vars&gt; &lt;clauses&gt;
    /// followed by clauses)
    [[nodiscard]] std::string to_dimacs() const;

    /// Computes a symmetric, normalized syntactic similarity score between
    /// this formula and another. With shared = shared_subformulae(other),
    /// f = shared / n_subformulae() and s = shared / other.n_subformulae(),
    /// the score is the harmonic mean 2 * f * s / (f + s); 0.0 when the two
    /// share nothing, and 1.0 when either side has zero subformulae. See
    /// syntactic_similarity in src/prop_formula/similarity.cpp for why the
    /// harmonic mean rather than the arithmetic one.
    ///
    /// The count is over the whole node arena, which is a multiset: there is
    /// no hash-consing, so `(a & a)` has three subformulae rather than two,
    /// and the shared count matches repeats with multiplicity.
    /// @param other The formula to compare with
    /// @return A similarity score in [0, 1]
    [[nodiscard]] double syntactic_similarity(const Formula& other) const;

    /// Returns the total number of subformulae (nodes) in this formula,
    /// including the root and all proper subformulae.
    /// @return The count of all nodes in the AST
    [[nodiscard]] std::size_t n_subformulae() const;

    /// Returns the number of subformulae shared with another formula.
    /// @param other The formula to compare with
    /// @return The number of shared subformulae
    [[nodiscard]] std::size_t shared_subformulae(const Formula& other) const;

    /// Converts the formula back to its string representation.
    /// Returns a string that, when parsed as a new Formula, produces an
    /// equivalent formula (minus whitespace and parentheses variations).
    /// @return A string representation of the formula
    [[nodiscard]] std::string to_string() const;

    /// Returns true if this formula contains no temporal operators, i.e. it is
    /// a purely propositional formula. Used to enforce that FRETISH conditions
    /// and responses never carry temporal structure, and as a precondition
    /// guard on propositional-only operations (to_dimacs, Tseitin CNF).
    [[nodiscard]] bool is_propositional() const;

    /// Semantics-preserving normal form, for use as a cache key. Flattens the
    /// commutative operators, orders and deduplicates their operands, and
    /// drops double negation, so that `a & (b & c)`, `(c & b) & a` and
    /// `!!a & b & c & b` all render identically.
    ///
    /// This exists because a cache key is a string and `Formula` is a binary
    /// tree with no canonical shape, so the search hands the solvers one
    /// formula under many spellings and each spelling buys its own
    /// subprocess. Measured over 14 specifications, ordering alone accounts
    /// for 16.4% of the `ltlfilt --simplify` execs a run then made.
    ///
    /// It is deliberately not a simplifier: no constant is folded and no
    /// tautology recognised, both of which belong to `simplify()`. And it is
    /// deliberately not applied to the stored formula, only to the key --
    /// re-associating a tree changes what `rewrite_post_order` walks, and
    /// with it the RNG draw sequence a seeded run reproduces.
    [[nodiscard]] Formula canonical() const;

    [[nodiscard]] std::size_t hash() const noexcept;

   private:
    struct Impl;
    /// Shared and const, not owned outright: nothing mutates a Formula's node
    /// arena in place -- simplify() and remove_double_negation() reassign the
    /// whole value -- so a copy can alias the arena instead of duplicating it.
    /// That makes Formula an immutable value type whose copies are a refcount
    /// bump, and lets operator< / operator== short-circuit on pointer
    /// identity.
    std::shared_ptr<const Impl> m_impl;

    /// Wraps an already-built node arena (root last) in a Formula. Used by the
    /// temporal construction/extraction path in transform.cpp.
    static Formula from_node_arena(
        std::vector<prop_formula_internal::Node> nodes);

    friend bool operator<(const Formula& lhs, const Formula& rhs);
    friend bool operator==(const Formula& lhs, const Formula& rhs);
};

bool operator==(const Formula& lhs, const Formula& rhs);

/// \cond
namespace std {  // NOLINT(build/namespaces)
template <>
struct hash<Formula> {
    std::size_t operator()(const Formula& formula) const noexcept {
        return formula.hash();
    }
};
}  // namespace std
/// \endcond
