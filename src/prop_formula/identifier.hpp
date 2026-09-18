#pragma once

#include <cctype>

// The identifier rule shared by the propositional parser, the TLSF lexer and
// the runners that scan SPOT's and black's formula text: a letter or
// underscore, then letters, digits and underscores.

inline bool is_identifier_start(char chr) {
    return (std::isalpha(static_cast<unsigned char>(chr)) != 0) || chr == '_';
}

inline bool is_identifier_char(char chr) {
    return (std::isalnum(static_cast<unsigned char>(chr)) != 0) || chr == '_';
}
