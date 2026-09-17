#ifndef PARSER_H
#define PARSER_H

#include <exception>
#include <string>
#include <vector>
#include "ast.h"
#include "scanner.h"
#include "token.h"

class ErrorSintaxis : public std::exception
{
public:
    std::string mensaje;
    int linea;
    int columna;
    std::string encontrado;
    std::string esperado;

    ErrorSintaxis(std::string msg, int l, int c, std::string enc, std::string esp)
        : mensaje(std::move(msg)), linea(l), columna(c),
          encontrado(std::move(enc)), esperado(std::move(esp)) {}

    const char *what() const noexcept override { return mensaje.c_str(); }
};

class Parser
{
public:
    explicit Parser(Scanner &scanner);
    ~Parser();

    Parser(const Parser &) = delete;
    Parser &operator=(const Parser &) = delete;

    // Program ::= StmList
    StatementList *parseProgram();

private:
    Scanner &scanner_;
    Token *current_;
    Token *previous_;

    static constexpr int MAX_PROFUNDIDAD = 200;
    int profundidad_ = 0;

    bool check(Token::Type t) const;
    bool match(Token::Type t);
    void advance();
    bool isAtEnd() const;
    void expect(Token::Type t, const char *contexto);
    std::string expectId(const char *contexto);
    long long expectNum(const char *contexto);
    [[noreturn]] void error(const std::string &mensaje, const char *esperado = "");

    Stm *parseStatement();
    CreateTableStm *parseCreateTable();
    CreateIndexStm *parseCreateIndex();
    DropTableStm *parseDropTable();
    SelectStm *parseSelect();
    InsertStm *parseInsert();
    DeleteStm *parseDelete();
    UpdateStm *parseUpdate();
    TxStm *parseTx();

    ColumnaDef parseColDec();
    DataType parseType(int &longitud);
    void parseConstraint(ColumnaDef &col);
    FromFileClause parseFromFile();
    IndexKind parseIndexType();

    std::vector<SelItem> parseSelList();
    FromClause parseSource();
    TableRef parseTableRef();
    Exp *parseWhere();
    GroupByClause parseGroupBy();
    std::vector<OrderItem> parseOrderBy();
    LimitClause parseLimit();

    std::vector<Exp *> parseExpList();
    Exp *parseCExp();
    Exp *parseOrExp();
    Exp *parseAndExp();
    Exp *parseNotExp();
    Exp *parseCompExp();
    Exp *parseExp();
    Exp *parseTerm();
    Exp *parseFactor();
};

#endif
