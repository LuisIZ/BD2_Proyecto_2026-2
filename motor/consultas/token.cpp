#include <iostream>
#include "token.h"

using namespace std;

Token::Token(Type type, int line, int column)
    : type(type), text(""), line(line), column(column) {}

Token::Token(Type type, char c, int line, int column)
    : type(type), text(string(1, c)), line(line), column(column) {}

Token::Token(Type type, const string &source, int first, int length, int line, int column)
    : type(type), text(source.substr(first, length)), line(line), column(column) {}

const char *Token::typeName(Type t)
{
    switch (t)
    {
    case CREATE: return "CREATE";
    case TABLE: return "TABLE";
    case ID: return "ID";
    case LP: return "(";
    case RP: return ")";
    case COMMA: return ",";
    case INT: return "INT";
    case VARCHAR: return "VARCHAR";
    case PRIMARY: return "PRIMARY";
    case KEY: return "KEY";
    case UNIQUE: return "UNIQUE";
    case INDEX: return "INDEX";
    case FROM: return "FROM";
    case FILE_KW: return "FILE";
    case USING: return "USING";
    case SEQUENTIAL: return "SEQUENTIAL";
    case HEAP: return "HEAP";
    case BTREE: return "BTREE";
    case HASH: return "HASH";
    case ON: return "ON";
    case DROP: return "DROP";
    case SELECT: return "SELECT";
    case AS: return "AS";
    case JOIN: return "JOIN";
    case WHERE: return "WHERE";
    case GROUP: return "GROUP";
    case BY: return "BY";
    case HAVING: return "HAVING";
    case ORDER: return "ORDER";
    case ASC: return "ASC";
    case DESC: return "DESC";
    case LIMIT: return "LIMIT";
    case OFFSET: return "OFFSET";
    case INSERT: return "INSERT";
    case INTO: return "INTO";
    case VALUES: return "VALUES";
    case DELETE_KW: return "DELETE";
    case UPDATE: return "UPDATE";
    case SET: return "SET";
    case BEGIN_KW: return "BEGIN";
    case COMMIT: return "COMMIT";
    case END_KW: return "END";
    case TRANSACTION: return "TRANSACTION";
    case ROLLBACK: return "ROLLBACK";
    case OR: return "OR";
    case AND: return "AND";
    case NOT: return "NOT";
    case BETWEEN: return "BETWEEN";
    case IN: return "IN";
    case LIKE: return "LIKE";
    case EQ: return "=";
    case NE: return "<>";
    case LT: return "<";
    case LE: return "<=";
    case GT: return ">";
    case GE: return ">=";
    case PLUS: return "+";
    case MINUS: return "-";
    case STAR: return "*";
    case PERCENT: return "%";
    case DOT: return ".";
    case INNER: return "INNER";
    case LEFT: return "LEFT";
    case RIGHT: return "RIGHT";
    case OUTER: return "OUTER";
    case FULL: return "FULL";
    case NUM: return "NUM";
    case STR: return "STR";
    case SEMICOLON: return ";";
    case END_INPUT: return "END_INPUT";
    case ERR: return "ERR";
    }
    return "UNKNOWN";
}

std::ostream &operator<<(std::ostream &outs, const Token &tok)
{
    switch (tok.type)
    {
    case Token::ID:
    case Token::NUM:
    case Token::STR:
    case Token::ERR:
        outs << "TOKEN(" << Token::typeName(tok.type) << ", \"" << tok.text << "\")";
        break;
    default:
        outs << "TOKEN(" << Token::typeName(tok.type) << ")";
        break;
    }
    return outs;
}

std::ostream &operator<<(std::ostream &outs, const Token *tok)
{
    return outs << *tok;
}
