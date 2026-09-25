#pragma once

#include <string>

// A formula as the stdin of a SPOT tool run with `-F -`, which is how every
// runner hands one over: an argv string is capped at MAX_ARG_STRLEN (128 KiB
// on Linux), and a lowered specification can exceed it.
//
// `-F` reads one formula per line, so a line break inside the formula would
// silently split it into two queries. Both line-break characters are
// whitespace to SPOT's parser, so replacing them with spaces keeps the
// meaning.
inline std::string spot_formula_line(const std::string& formula) {
    std::string line;
    line.reserve(formula.size() + 1);
    for (const char character : formula) {
        line.push_back(character == '\n' || character == '\r' ? ' '
                                                              : character);
    }
    line.push_back('\n');
    return line;
}
