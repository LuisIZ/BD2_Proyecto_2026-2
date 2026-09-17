#include <iostream>
#include <cctype>
#include <cstring>
#include "token.h"
#include "scanner.h"

using namespace std;

Scanner::Scanner(const char *s) : input(s), first(0), current(0), line(1), line_start(0) {}

static bool is_white_space(char c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static string to_lower(const string &s)
{
    string out = s;
    for (char &c : out)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return out;
}

const unordered_map<string, Token::Type> &Scanner::keywords()
{
    static const unordered_map<string, Token::Type> table = {
        {"create", Token::CREATE},
        {"table", Token::TABLE},
        {"int", Token::INT},
        {"varchar", Token::VARCHAR},
        {"primary", Token::PRIMARY},
        {"key", Token::KEY},
        {"unique", Token::UNIQUE},
        {"index", Token::INDEX},
        {"from", Token::FROM},
        {"file", Token::FILE_KW},
        {"using", Token::USING},
        {"sequential", Token::SEQUENTIAL},
        {"heap", Token::HEAP},
        {"btree", Token::BTREE},
        {"hash", Token::HASH},
        {"on", Token::ON},
        {"drop", Token::DROP},
        {"select", Token::SELECT},
        {"as", Token::AS},
        {"join", Token::JOIN},
        {"where", Token::WHERE},
        {"group", Token::GROUP},
        {"by", Token::BY},
        {"having", Token::HAVING},
        {"order", Token::ORDER},
        {"asc", Token::ASC},
        {"desc", Token::DESC},
        {"limit", Token::LIMIT},
        {"offset", Token::OFFSET},
        {"insert", Token::INSERT},
        {"into", Token::INTO},
        {"values", Token::VALUES},
        {"delete", Token::DELETE_KW},
        {"update", Token::UPDATE},
        {"set", Token::SET},
        {"begin", Token::BEGIN_KW},
        {"commit", Token::COMMIT},
        {"end", Token::END_KW},
        {"transaction", Token::TRANSACTION},
        {"rollback", Token::ROLLBACK},
        {"or", Token::OR},
        {"and", Token::AND},
        {"not", Token::NOT},
        {"between", Token::BETWEEN},
        {"in", Token::IN},
        {"like", Token::LIKE},

        // reservadas sin uso en la Parte 1, para que "t LEFT JOIN u" no lea
        // LEFT como alias de t
        {"inner", Token::INNER},
        {"left", Token::LEFT},
        {"right", Token::RIGHT},
        {"outer", Token::OUTER},
        {"full", Token::FULL},
    };
    return table;
}

void Scanner::skipWhitespaceAndComments()
{
    bool progress = true;
    while (progress)
    {
        progress = false;

        while (current < (int)input.length() && is_white_space(input[current]))
        {
            if (input[current] == '\n')
            {
                line++;
                line_start = current + 1;
            }
            current++;
            progress = true;
        }

        if (current + 1 < (int)input.length() && input[current] == '-' && input[current + 1] == '-')
        {
            while (current < (int)input.length() && input[current] != '\n')
                current++;
            progress = true;
        }
    }
}

Token *Scanner::nextToken()
{
    skipWhitespaceAndComments();

    if (current >= (int)input.length())
        return new Token(Token::END_INPUT, line, columnOf(current));

    char c = input[current];
    first = current;
    int col = columnOf(first);

    if (isdigit(static_cast<unsigned char>(c)))
    {
        while (current < (int)input.length() && isdigit(static_cast<unsigned char>(input[current])))
            current++;

        // solo enteros en la Parte 1; un decimal se consume completo para
        // reportarlo como error de un solo lexema
        if (current + 1 < (int)input.length() && input[current] == '.' &&
            isdigit(static_cast<unsigned char>(input[current + 1])))
        {
            current++;
            while (current < (int)input.length() && isdigit(static_cast<unsigned char>(input[current])))
                current++;
            return new Token(Token::ERR, input, first, current - first, line, col);
        }

        return new Token(Token::NUM, input, first, current - first, line, col);
    }

    if (c == '\'')
    {
        current++;
        string value;
        bool closed = false;

        while (current < (int)input.length())
        {
            if (input[current] == '\'')
            {
                if (current + 1 < (int)input.length() && input[current + 1] == '\'')
                {
                    value += '\'';
                    current += 2;
                    continue;
                }
                current++;
                closed = true;
                break;
            }
            if (input[current] == '\n')
            {
                line++;
                line_start = current + 1;
            }
            value += input[current];
            current++;
        }

        if (!closed)
            return new Token(Token::ERR, input, first, current - first, line, col);

        Token *t = new Token(Token::STR, line, col);
        t->text = value;
        return t;
    }

    if (isalpha(static_cast<unsigned char>(c)) || c == '_')
    {
        current++;
        while (current < (int)input.length() &&
               (isalnum(static_cast<unsigned char>(input[current])) || input[current] == '_'))
            current++;

        string word = input.substr(first, current - first);

        auto it = keywords().find(to_lower(word));
        if (it != keywords().end())
            return new Token(it->second, word, 0, word.length(), line, col);

        return new Token(Token::ID, word, 0, word.length(), line, col);
    }

    Token *token;
    switch (c)
    {
    case '(':
        token = new Token(Token::LP, c, line, col);
        break;
    case ')':
        token = new Token(Token::RP, c, line, col);
        break;
    case ',':
        token = new Token(Token::COMMA, c, line, col);
        break;
    case ';':
        token = new Token(Token::SEMICOLON, c, line, col);
        break;
    case '.':
        token = new Token(Token::DOT, c, line, col);
        break;
    case '+':
        token = new Token(Token::PLUS, c, line, col);
        break;
    case '-':
        token = new Token(Token::MINUS, c, line, col);
        break;
    case '*':
        token = new Token(Token::STAR, c, line, col);
        break;
    case '%':
        token = new Token(Token::PERCENT, c, line, col);
        break;
    case '=':
        token = new Token(Token::EQ, c, line, col);
        break;
    case '<':
        if (current + 1 < (int)input.length() && input[current + 1] == '=')
        {
            token = new Token(Token::LE, "<=", 0, 2, line, col);
            current++;
        }
        else if (current + 1 < (int)input.length() && input[current + 1] == '>')
        {
            token = new Token(Token::NE, "<>", 0, 2, line, col);
            current++;
        }
        else
        {
            token = new Token(Token::LT, c, line, col);
        }
        break;
    case '>':
        if (current + 1 < (int)input.length() && input[current + 1] == '=')
        {
            token = new Token(Token::GE, ">=", 0, 2, line, col);
            current++;
        }
        else
        {
            token = new Token(Token::GT, c, line, col);
        }
        break;
    case '!':
        if (current + 1 < (int)input.length() && input[current + 1] == '=')
        {
            token = new Token(Token::NE, "!=", 0, 2, line, col);
            current++;
        }
        else
        {
            token = new Token(Token::ERR, c, line, col);
        }
        break;
    default:
        token = new Token(Token::ERR, c, line, col);
        break;
    }
    current++;
    return token;
}

void Scanner::reset()
{
    first = 0;
    current = 0;
    line = 1;
    line_start = 0;
}

Scanner::~Scanner() {}

void test_scanner(Scanner *scanner)
{
    Token *current;
    cout << "Initiating Scanner:" << endl
         << endl;
    while ((current = scanner->nextToken())->type != Token::END_INPUT)
    {
        if (current->type == Token::ERR)
        {
            cout << "Error in scanner - line " << current->line
                 << ", column " << current->column
                 << ": invalid token \"" << current->text << "\"" << endl;
            delete current;
            return;
        }
        cout << *current << endl;
        delete current;
    }
    cout << "TOKEN(END_INPUT)" << endl;
    delete current;
}
