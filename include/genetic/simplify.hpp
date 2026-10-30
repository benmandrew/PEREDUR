#pragma once

/// @file simplify.hpp
/// @brief The simplification step every bred FRETISH offspring takes.

#include "requirement.hpp"

/// Simplifies each weakenable, live requirement of @p offspring in place: its
/// condition, its response and any stop condition, with the lowered LTL
/// recomputed. Locked and removed requirements are left alone. If the
/// simplification makes two requirements equal, so that rebuilding the
/// specification drops one, @p offspring is returned unsimplified instead.
Specification simplify_offspring(Specification offspring);
