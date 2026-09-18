#include "lexer.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "prop_formula/identifier.hpp"

namespace tlsf::internal {

namespace {

struct SymbolEntry {
    std::string_view m_lexeme;
    Tok m_type{};
};

// Tried in order, so an entry whose lexeme is a prefix of another must come
// after it -- otherwise the shorter one wins and the longer is unreachable.
constexpr std::array k_multi_char_symbols = {
    SymbolEntry{"<->", Tok::Iff},   SymbolEntry{"->", Tok::Implies},
    SymbolEntry{"&&", Tok::AndAnd}, SymbolEntry{"||", Tok::OrOr},
    SymbolEntry{"..", Tok::DotDot},
};

// `-`, `<` and `.` are deliberately absent: alone they are not TLSF tokens, so
// they fall through to Tok::Unknown and the parser reports them as such.
constexpr std::array k_single_char_symbols = {
    SymbolEntry{"{", Tok::LBrace},    SymbolEntry{"}", Tok::RBrace},
    SymbolEntry{"(", Tok::LParen},    SymbolEntry{")", Tok::RParen},
    SymbolEntry{"[", Tok::LBracket},  SymbolEntry{"]", Tok::RBracket},
    SymbolEntry{";", Tok::Semicolon}, SymbolEntry{":", Tok::Colon},
    SymbolEntry{",", Tok::Comma},     SymbolEntry{"@", Tok::At},
    SymbolEntry{"'", Tok::Prime},     SymbolEntry{"!", Tok::Not},
    SymbolEntry{"&", Tok::And},       SymbolEntry{"|", Tok::Or},
};

class Lexer {
   public:
    explicit Lexer(const std::string& text) : m_text(text) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (true) {
            Token token = next_token();
            const bool is_end = token.m_type == Tok::End;
            tokens.push_back(std::move(token));
            if (is_end) {
                break;
            }
        }
        return tokens;
    }

   private:
    const std::string& m_text;
    std::size_t m_pos = 0;

    // Reads past the end as '\0', which no lexeme contains, so lookahead needs
    // no bounds check of its own.
    [[nodiscard]] char peek(std::size_t offset = 0) const {
        const std::size_t idx = m_pos + offset;
        return idx < m_text.size() ? m_text[idx] : '\0';
    }

    [[nodiscard]] bool starts_with(std::string_view lexeme) const {
        for (std::size_t i = 0; i < lexeme.size(); ++i) {
            if (peek(i) != lexeme[i]) {
                return false;
            }
        }
        return true;
    }

    void skip_trivia() {
        while (m_pos < m_text.size()) {
            const char character = m_text[m_pos];
            if (std::isspace(static_cast<unsigned char>(character)) != 0) {
                ++m_pos;
                continue;
            }
            if (character == '/' && peek(1) == '/') {
                skip_line_comment();
                continue;
            }
            if (character == '/' && peek(1) == '*') {
                skip_block_comment();
                continue;
            }
            break;
        }
    }

    void skip_line_comment() {
        while (m_pos < m_text.size() && m_text[m_pos] != '\n') {
            ++m_pos;
        }
    }

    // An unterminated block comment runs to end of input rather than throwing.
    void skip_block_comment() {
        m_pos += 2;
        while (m_pos < m_text.size() &&
               (m_text[m_pos] != '*' || peek(1) != '/')) {
            ++m_pos;
        }
        if (m_pos < m_text.size()) {
            m_pos += 2;
        }
    }

    Token next_token() {
        skip_trivia();
        if (m_pos >= m_text.size()) {
            return {Tok::End, ""};
        }
        const char character = m_text[m_pos];
        if (is_identifier_start(character)) {
            return lex_ident();
        }
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            return lex_number();
        }
        if (character == '"') {
            return lex_string();
        }
        return lex_symbol();
    }

    Token lex_ident() {
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && is_identifier_char(m_text[m_pos])) {
            ++m_pos;
        }
        return {Tok::Ident, m_text.substr(start, m_pos - start)};
    }

    Token lex_number() {
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() &&
               std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
            ++m_pos;
        }
        return {Tok::Number, m_text.substr(start, m_pos - start)};
    }

    Token lex_string() {
        ++m_pos;  // opening quote
        std::string value;
        while (m_pos < m_text.size() && m_text[m_pos] != '"') {
            if (m_text[m_pos] == '\\' && m_pos + 1 < m_text.size()) {
                value.push_back(m_text[m_pos + 1]);
                m_pos += 2;
                continue;
            }
            value.push_back(m_text[m_pos]);
            ++m_pos;
        }
        if (m_pos >= m_text.size()) {
            throw std::invalid_argument(
                "TLSF parse error: unterminated string");
        }
        ++m_pos;  // closing quote
        return {Tok::String, value};
    }

    Token lex_symbol() {
        for (const SymbolEntry& symbol : k_multi_char_symbols) {
            if (starts_with(symbol.m_lexeme)) {
                m_pos += symbol.m_lexeme.size();
                return {symbol.m_type, std::string(symbol.m_lexeme)};
            }
        }
        const char character = m_text[m_pos];
        ++m_pos;
        for (const SymbolEntry& symbol : k_single_char_symbols) {
            if (symbol.m_lexeme[0] == character) {
                return {symbol.m_type, std::string(symbol.m_lexeme)};
            }
        }
        return {Tok::Unknown, std::string(1, character)};
    }
};

}  // namespace

std::vector<Token> tokenize(const std::string& text) {
    Lexer lexer(text);
    return lexer.tokenize();
}

}  // namespace tlsf::internal
