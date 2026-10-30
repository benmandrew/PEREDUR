#pragma once

/// @file formula_key.hpp
/// @brief Canonical cache keys for LTL formula strings.
///
/// Every memoisation cache between the search and the external tools is keyed
/// on a rendered formula string, and `Formula` is a binary tree with no
/// canonical shape, so one formula reaches a cache under many spellings and
/// each spelling buys its own subprocess. Measured over 14 specifications,
/// replacing the raw string with the key these functions produce removes
/// 22.1% of the `ltlfilt --simplify` execs a run then made, 13.9% of
/// `ltlsynt`'s and 11.3% of `count_traces`'s; where the atom renaming also
/// applies, those become 31.6%, 16.2% and 15.7%.
///
/// The keys are computed from the string rather than from the AST because the
/// caches receive strings, and because the same formula reaches several of
/// them by different routes. Each entry point memoises on its raw input, so a
/// spelling already seen costs one hash rather than a parse.
///
/// Which key a cache may take is decided by what it stores. One holding a
/// verdict or a count may take the renamed form, those being invariant under
/// a bijection on the atoms. One holding a formula or an automaton may not:
/// reading such a value back would mean renaming atoms inside a tool's own
/// output, and SPOT prints a unary operator hard against its operand
/// (`Ffk1`), so no tokenisation of that output separates the operator from
/// the name. Those caches take `canonical`, which preserves atom names.

#include <string>
#include <vector>

namespace formula_key {

/// The rendering of @p ltl's canonical form (see `Formula::canonical`). Atom
/// names are preserved, so a value that names atoms may be stored under this
/// key and handed back to any caller unchanged.
///
/// The reference stays valid for the life of the process: the memo behind it
/// is append-only, and `std::unordered_map` keeps references to its elements
/// valid across a rehash. Returning by value instead would copy the key on
/// every lookup, which is most of what this is here to avoid.
const std::string& canonical(const std::string& ltl);

/// A canonical key whose atoms are renamed to a canonical sequence in order of
/// first appearance, so that two formulae differing only in their atom names
/// share it. Sound only where the cached value is invariant under a bijective
/// renaming. The boolean constants are ordinary atoms by convention and are
/// left alone, renaming one being how a constant becomes a free variable.
const std::string& renamed(const std::string& ltl);

/// A canonical key for a realizability query. The renaming preserves the
/// input/output partition, since realizability is invariant under a bijection
/// only when it does, and the key carries the number of declared signals the
/// formula never mentions -- interchangeable among themselves, but part of the
/// alphabet the synthesiser plays over.
const std::string& realizability(const std::string& ltl,
                                 const std::vector<std::string>& inputs,
                                 const std::vector<std::string>& outputs);

}  // namespace formula_key
