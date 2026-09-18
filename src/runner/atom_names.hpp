#pragma once

// Rejects atom names SPOT mishandles silently.
//
// Two failures, both measured against SPOT 2.15.1 and neither of them visible
// in any output:
//
//   - `ltlsynt --ins` never matches a name containing an uppercase letter. The
//     engine passes `--ins` alone and lets everything else default to an
//     output, so an uppercase *input* silently becomes an output and the
//     verdict errs towards realizable: `ltlsynt --ins=A_in -f 'G A_in'` answers
//     REALIZABLE where the lowercase spelling answers UNREALIZABLE. Uppercase
//     in an output name is harmless, since outputs are never passed.
//   - A name beginning with `F`, `G` or `X` followed by a letter or an
//     underscore loses that letter to the temporal operator: SPOT reads `Fail`
//     as `F(ail)`, `Gate` as `G(ate)` and `G_a` as `G(_a)`. A digit is safe
//     (`F1`, `G0` parse whole). SPOT prints `F ail` back as `Fail`, so a
//     round-trip through text never reveals it. This one is live on the TLSF
//     path, which passes spec names raw; the FRETISH path is already covered
//     by `k_atom_prefix`, which puts `iap_` in front of every atom.
//
// Both produce wrong answers with nothing in the output to say so, which is
// why this is a rejection at load rather than a warning.

#include <optional>
#include <string>
#include <vector>

namespace runner {

/// The first name in @p inputs or @p outputs that SPOT would mishandle, with
/// what is wrong with it, or nullopt when every name is safe. The two lists
/// are checked against different rules: the uppercase rule applies to inputs
/// alone, the leading-operator rule to both.
std::optional<std::string> first_unsafe_atom_name(
    const std::vector<std::string>& inputs,
    const std::vector<std::string>& outputs);

}  // namespace runner
