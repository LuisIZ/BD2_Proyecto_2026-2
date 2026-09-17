#ifndef TOKEN_H
#define TOKEN_H

#include <string>

class Token
{
public:
    enum Type
    {
        // CreateTableStm
        CREATE,
        TABLE,
        ID,
        LP,
        RP,

        // ColDecList
        COMMA,

        // Type
        INT,
        VARCHAR,

        // Constraint
        PRIMARY,
        KEY,
        UNIQUE,
        INDEX,

        // FromFile
        FROM,
        FILE_KW, // "FILE" choca con la macro de <cstdio>
        USING,

        // IndexType
        SEQUENTIAL,
        HEAP,
        BTREE,
        HASH,

        // CreateIndexStm
        ON,

        // DropTableStm
        DROP,

        // SelectStm
        SELECT,

        // SelItem
        AS,

        // Join
        JOIN,

        // Where
        WHERE,

        // GroupBy
        GROUP,
        BY,
        HAVING,

        // OrderBy
        ORDER,

        // Direction
        ASC,
        DESC,

        // Limit
        LIMIT,
        OFFSET,

        // InsertStm
        INSERT,
        INTO,
        VALUES,

        // DeleteStm
        DELETE_KW, // palabra reservada en algunos headers de Windows

        // UpdateStm
        UPDATE,
        SET,

        // TxStm
        BEGIN_KW,
        COMMIT,
        END_KW, // "end transaction", no fin de entrada
        TRANSACTION,
        ROLLBACK,

        // OrExp
        OR,

        // AndExp
        AND,

        // NotExp
        NOT,

        // CompTail
        BETWEEN,
        IN,
        LIKE,

        // CompOp
        EQ,
        NE,
        LT,
        LE,
        GT,
        GE,

        // Exp
        PLUS,
        MINUS,

        // Term
        STAR,
        PERCENT,

        // Column
        DOT,

        // Reserved, sin uso en la Parte 1
        INNER,
        LEFT,
        RIGHT,
        OUTER,
        FULL,

        NUM,
        STR,

        SEMICOLON,
        END_INPUT,
        ERR
    };

    Type type;
    std::string text;
    int line;
    int column;

    Token(Type type, int line = 1, int column = 1);
    Token(Type type, char c, int line, int column);
    Token(Type type, const std::string &source, int first, int length, int line, int column);

    static const char *typeName(Type t);

    friend std::ostream &operator<<(std::ostream &outs, const Token &tok);
    friend std::ostream &operator<<(std::ostream &outs, const Token *tok);
};

#endif
