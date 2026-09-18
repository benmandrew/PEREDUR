#include <cassert>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "identifier.hpp"
#include "internal.hpp"

namespace {

class Parser {
   private:
    std::string m_text;
    std::size_t m_position = 0;
    std::vector<prop_formula_internal::Node> m_nodes;
    bool m_failed = false;

   public:
    explicit Parser(std::string text) : m_text(std::move(text)) {}

    [[nodiscard]] bool failed() const { return m_failed; }

    // Always returns a non-empty, root-last arena, malformed input included:
    // every production pushes a node, so the walk has something to return
    // whatever it was handed. Whether the string was well formed is `failed()`,
    // and what to do about that belongs to the caller -- parse_formula asserts,
    // try_parse_formula reports. Asserting in here would fire on both.
    std::vector<prop_formula_internal::Node> parse() {
        m_position = 0;
        m_nodes.clear();
        m_failed = false;
        // Only the assert below reads this, so it is unused under NDEBUG.
        [[maybe_unused]] const std::size_t root = parse_iff();
        skip_whitespace();
        if (!at_end() || m_nodes.empty()) {
            m_failed = true;
        }
        // Root-last is an invariant every arena consumer relies on, and it
        // holds by construction: each production returns the index of the most
        // recently pushed node. It used to be "restored" here by erasing the
        // root and re-pushing it, which could only ever have corrupted the
        // arena -- erase shifts every later node down one without remapping the
        // m_left/m_right indices pointing at them. Assert it instead.
        assert(m_nodes.empty() || root == m_nodes.size() - 1);
        return m_nodes;
    }

   private:
    std::size_t parse_iff() {
        std::size_t lhs = parse_implies();
        while (try_consume("<->")) {
            const std::size_t rhs = parse_implies();
            lhs = push_binary(prop_formula_internal::NodeType::Iff, lhs, rhs);
        }
        return lhs;
    }

    std::size_t parse_implies() {
        std::size_t lhs = parse_or();
        if (try_consume("->")) {
            const std::size_t rhs = parse_implies();
            lhs =
                push_binary(prop_formula_internal::NodeType::Implies, lhs, rhs);
        }
        return lhs;
    }

    std::size_t parse_or() {
        std::size_t lhs = parse_and();
        while (try_consume("|")) {
            const std::size_t rhs = parse_and();
            lhs = push_binary(prop_formula_internal::NodeType::Or, lhs, rhs);
        }
        return lhs;
    }

    std::size_t parse_and() {
        std::size_t lhs = parse_temporal_binary();
        while (try_consume("&")) {
            const std::size_t rhs = parse_temporal_binary();
            lhs = push_binary(prop_formula_internal::NodeType::And, lhs, rhs);
        }
        return lhs;
    }

    // U, R and W. Left-associative like the propositional connectives, which
    // costs nothing here: node_to_string parenthesises every operand, so the
    // only strings this has to agree with carry their own association.
    std::size_t parse_temporal_binary() {
        std::size_t lhs = parse_unary();
        while (true) {
            const std::optional<prop_formula_internal::NodeType> type =
                try_consume_temporal_binary();
            if (!type) {
                return lhs;
            }
            const std::size_t rhs = parse_unary();
            lhs = push_binary(*type, lhs, rhs);
        }
    }

    std::optional<prop_formula_internal::NodeType>
    try_consume_temporal_binary() {
        if (try_consume_keyword("U")) {
            return prop_formula_internal::NodeType::Until;
        }
        if (try_consume_keyword("R")) {
            return prop_formula_internal::NodeType::Release;
        }
        if (try_consume_keyword("W")) {
            return prop_formula_internal::NodeType::WeakUntil;
        }
        return std::nullopt;
    }

    std::size_t parse_unary() {
        if (try_consume("!") || try_consume("~")) {
            const std::size_t child = parse_unary();
            return push_unary(prop_formula_internal::NodeType::Not, child);
        }
        if (try_consume_keyword("X")) {
            return push_unary(prop_formula_internal::NodeType::Next,
                              parse_unary());
        }
        if (try_consume_keyword("F")) {
            return push_unary(prop_formula_internal::NodeType::Eventually,
                              parse_unary());
        }
        if (try_consume_keyword("G")) {
            return push_unary(prop_formula_internal::NodeType::Globally,
                              parse_unary());
        }
        if (try_consume("(")) {
            const std::size_t expression = parse_iff();
            if (!try_consume(")")) {
                m_failed = true;
            }
            return expression;
        }
        return parse_variable();
    }

    std::size_t parse_variable() {
        skip_whitespace();
        // An empty name rather than a read past the end: every production
        // above returns an index, so the walk has to produce a node even on a
        // string it cannot read.
        if (at_end()) {
            m_failed = true;
            m_nodes.push_back(prop_formula_internal::Node{
                prop_formula_internal::NodeType::Variable, "", 0, 0});
            return m_nodes.size() - 1;
        }
        const char first = m_text[m_position];
        if (!is_identifier_start(first)) {
            m_failed = true;
        }
        std::string name;
        name.push_back(first);
        ++m_position;
        while (!at_end()) {
            const char character = m_text[m_position];
            if (is_identifier_char(character)) {
                name.push_back(character);
                ++m_position;
                continue;
            }
            break;
        }

        m_nodes.push_back(prop_formula_internal::Node{
            prop_formula_internal::NodeType::Variable, name, 0, 0});
        return m_nodes.size() - 1;
    }

    std::size_t push_unary(prop_formula_internal::NodeType type,
                           std::size_t child) {
        m_nodes.push_back(prop_formula_internal::Node{type, "", child, 0});
        return m_nodes.size() - 1;
    }

    std::size_t push_binary(prop_formula_internal::NodeType type,
                            std::size_t lhs, std::size_t rhs) {
        m_nodes.push_back(prop_formula_internal::Node{type, "", lhs, rhs});
        return m_nodes.size() - 1;
    }

    // An operator spelled as a letter is only an operator when the next
    // character cannot continue an identifier, so `Grant` lexes as one atom
    // and `G(rant)` as the operator. Atoms carry an `iap_` prefix on the
    // search path, but the TLSF corpus and the tests use bare names.
    bool try_consume_keyword(const std::string& token) {
        skip_whitespace();
        if (m_position > m_text.size() ||
            m_text.compare(m_position, token.size(), token) != 0) {
            return false;
        }
        const std::size_t after = m_position + token.size();
        if (after < m_text.size()) {
            const char next = m_text[after];
            if (is_identifier_char(next)) {
                return false;
            }
        }
        m_position = after;
        return true;
    }

    bool try_consume(const std::string& token) {
        skip_whitespace();
        if (m_position > m_text.size()) {
            return false;
        }
        if (m_text.compare(m_position, token.size(), token) == 0) {
            m_position += token.size();
            return true;
        }
        return false;
    }

    void skip_whitespace() {
        while (!at_end() && (std::isspace(static_cast<unsigned char>(
                                 m_text[m_position])) != 0)) {
            ++m_position;
        }
    }

    [[nodiscard]] bool at_end() const { return m_position >= m_text.size(); }
};

}  // namespace

namespace prop_formula_internal {

std::vector<Node> parse_formula(const std::string& formula) {
    Parser parser(formula);
    std::vector<Node> nodes = parser.parse();
    // The asserting entry point, unchanged in what it promises: a caller here
    // built its own input, so a string this cannot read is a defect. Under
    // NDEBUG it returns the partial arena, as it always has.
    assert(!parser.failed());
    return nodes;
}

std::optional<std::vector<Node>> try_parse_formula(const std::string& formula) {
    Parser parser(formula);
    std::vector<Node> nodes = parser.parse();
    if (parser.failed()) {
        return std::nullopt;
    }
    return nodes;
}

}  // namespace prop_formula_internal

Formula::Impl::Impl(const std::string& formula)
    : m_nodes(prop_formula_internal::parse_formula(formula)) {}

std::optional<Formula> Formula::try_parse(const std::string& formula) {
    auto nodes = prop_formula_internal::try_parse_formula(formula);
    if (!nodes) {
        return std::nullopt;
    }
    Formula parsed;
    parsed.m_impl = std::make_shared<Impl>(std::move(*nodes));
    return parsed;
}
