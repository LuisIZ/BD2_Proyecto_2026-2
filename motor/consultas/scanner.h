#ifndef SCANNER_H
#define SCANNER_H

#include <string>
#include <unordered_map>
#include "token.h"

class Scanner
{
private:
    std::string input;
    int first, current;
    int line;
    int line_start;

    int columnOf(int pos) const { return pos - line_start + 1; }
    void skipWhitespaceAndComments();

    static const std::unordered_map<std::string, Token::Type> &keywords();

public:
    Scanner(const char *in_s);
    Token *nextToken();
    void reset();
    ~Scanner();
};

void test_scanner(Scanner *scanner);

#endif
