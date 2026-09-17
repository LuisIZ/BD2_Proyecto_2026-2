#ifndef AST_H
#define AST_H

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

enum class DataType
{
    INT,
    VARCHAR
};

enum class IndexKind
{
    SEQUENTIAL,
    HEAP,
    BTREE,
    HASH
};

enum class Direction
{
    ASC,
    DESC
};

enum class TxKind
{
    BEGIN_TX,
    COMMIT_TX,
    ROLLBACK_TX,
    END_TRANSACTION
};

const char *nombreDataType(DataType t);
const char *nombreIndexKind(IndexKind k);

class Exp
{
public:
    virtual ~Exp() = default;
    virtual void imprimir(std::ostream &out) const = 0;
};

class NumExp : public Exp
{
public:
    std::int64_t valor;
    explicit NumExp(std::int64_t v) : valor(v) {}
    void imprimir(std::ostream &out) const override;
};

class StrExp : public Exp
{
public:
    std::string valor;
    explicit StrExp(std::string v) : valor(std::move(v)) {}
    void imprimir(std::ostream &out) const override;
};

class ColumnExp : public Exp
{
public:
    std::string tabla; // vacio si no viene calificada
    std::string columna;
    int indice_resuelto = -1;

    ColumnExp(std::string t, std::string c) : tabla(std::move(t)), columna(std::move(c)) {}
    void imprimir(std::ostream &out) const override;
};

enum class BinaryOp
{
    OR,
    AND,
    EQ,
    NE,
    LT,
    LE,
    GT,
    GE,
    PLUS,
    MINUS,
    STAR,
    PERCENT
};

const char *nombreBinaryOp(BinaryOp op);

class BinaryExp : public Exp
{
public:
    Exp *izq;
    Exp *der;
    BinaryOp op;

    BinaryExp(Exp *i, Exp *d, BinaryOp o) : izq(i), der(d), op(o) {}
    ~BinaryExp() override
    {
        delete izq;
        delete der;
    }
    void imprimir(std::ostream &out) const override;
};

enum class UnaryOp
{
    NEG,
    NOT
};

class UnaryExp : public Exp
{
public:
    Exp *operando;
    UnaryOp op;

    UnaryExp(Exp *o, UnaryOp opr) : operando(o), op(opr) {}
    ~UnaryExp() override { delete operando; }
    void imprimir(std::ostream &out) const override;
};

class FCallExp : public Exp
{
public:
    std::string nombre;
    std::vector<Exp *> args;
    bool es_star;

    FCallExp(std::string n, std::vector<Exp *> a, bool star)
        : nombre(std::move(n)), args(std::move(a)), es_star(star) {}
    ~FCallExp() override
    {
        for (auto *a : args)
            delete a;
    }
    void imprimir(std::ostream &out) const override;
};

class BetweenExp : public Exp
{
public:
    Exp *valor;
    Exp *bajo;
    Exp *alto;
    bool negado;

    BetweenExp(Exp *v, Exp *b, Exp *a, bool n) : valor(v), bajo(b), alto(a), negado(n) {}
    ~BetweenExp() override
    {
        delete valor;
        delete bajo;
        delete alto;
    }
    void imprimir(std::ostream &out) const override;
};

class InExp : public Exp
{
public:
    Exp *valor;
    std::vector<Exp *> lista;
    bool negado;

    InExp(Exp *v, std::vector<Exp *> l, bool n) : valor(v), lista(std::move(l)), negado(n) {}
    ~InExp() override
    {
        delete valor;
        for (auto *e : lista)
            delete e;
    }
    void imprimir(std::ostream &out) const override;
};

class LikeExp : public Exp
{
public:
    Exp *valor;
    Exp *patron;
    bool negado;

    LikeExp(Exp *v, Exp *p, bool n) : valor(v), patron(p), negado(n) {}
    ~LikeExp() override
    {
        delete valor;
        delete patron;
    }
    void imprimir(std::ostream &out) const override;
};

// ColDec ::= id Type (Constraint)*
struct ColumnaDef
{
    std::string nombre;
    DataType tipo = DataType::INT;
    int longitud = 0;
    bool primary_key = false;
    bool unique = false;
    bool tiene_indice = false;
    IndexKind indice_tipo = IndexKind::HASH;
};

// FromFile ::= from file Str [using index IndexType (id)]
struct FromFileClause
{
    std::string ruta;
    bool tiene_indice = false;
    IndexKind indice_tipo = IndexKind::HASH;
    std::string columna_indice;
};

struct SelItem
{
    Exp *expresion = nullptr;
    std::string alias;
    bool es_star = false;
};

struct TableRef
{
    std::string tabla;
    std::string alias;
};

struct JoinClause
{
    TableRef tabla;
    Exp *condicion = nullptr;
};

struct FromClause
{
    TableRef base;
    std::vector<JoinClause> joins;
};

struct GroupByClause
{
    std::vector<Exp *> claves;
    Exp *having = nullptr;
};

struct OrderItem
{
    Exp *expresion = nullptr;
    Direction direccion = Direction::ASC;
};

struct LimitClause
{
    long long limite = 0;
    long long offset = 0;
    bool tiene_offset = false;
};

struct Assign
{
    std::string columna;
    Exp *valor = nullptr;
};

class StmVisitor;

class Stm
{
public:
    virtual ~Stm() = default;
    virtual void accept(StmVisitor &v) = 0;
};

class CreateTableStm : public Stm
{
public:
    std::string tabla;
    std::vector<ColumnaDef> columnas;
    bool tiene_archivo = false;
    FromFileClause archivo;

    void accept(StmVisitor &v) override;
};

class CreateIndexStm : public Stm
{
public:
    std::string nombre_indice;
    std::string tabla;
    IndexKind tipo = IndexKind::HASH;
    std::string columna;

    void accept(StmVisitor &v) override;
};

class DropTableStm : public Stm
{
public:
    std::string tabla;

    void accept(StmVisitor &v) override;
};

class SelectStm : public Stm
{
public:
    std::vector<SelItem> sel_list;
    FromClause from;
    Exp *where = nullptr;
    GroupByClause group_by;
    bool tiene_group_by = false;
    std::vector<OrderItem> order_by;
    LimitClause limit;
    bool tiene_limit = false;

    ~SelectStm() override;
    void accept(StmVisitor &v) override;
};

class InsertStm : public Stm
{
public:
    std::string tabla;
    std::vector<std::string> columnas;
    std::vector<std::vector<Exp *>> filas;

    ~InsertStm() override;
    void accept(StmVisitor &v) override;
};

class DeleteStm : public Stm
{
public:
    std::string tabla;
    Exp *where = nullptr;

    ~DeleteStm() override { delete where; }
    void accept(StmVisitor &v) override;
};

class UpdateStm : public Stm
{
public:
    std::string tabla;
    std::vector<Assign> asignaciones;
    Exp *where = nullptr;

    ~UpdateStm() override;
    void accept(StmVisitor &v) override;
};

class TxStm : public Stm
{
public:
    TxKind kind;
    explicit TxStm(TxKind k) : kind(k) {}

    void accept(StmVisitor &v) override;
};

class StatementList
{
public:
    std::vector<Stm *> sentencias;
    ~StatementList()
    {
        for (auto *s : sentencias)
            delete s;
    }
};

class StmVisitor
{
public:
    virtual ~StmVisitor() = default;
    virtual void visit(CreateTableStm &s) = 0;
    virtual void visit(CreateIndexStm &s) = 0;
    virtual void visit(DropTableStm &s) = 0;
    virtual void visit(SelectStm &s) = 0;
    virtual void visit(InsertStm &s) = 0;
    virtual void visit(DeleteStm &s) = 0;
    virtual void visit(UpdateStm &s) = 0;
    virtual void visit(TxStm &s) = 0;
};

class PrintVisitor : public StmVisitor
{
public:
    explicit PrintVisitor(std::ostream &out) : out_(out) {}

    void visit(CreateTableStm &s) override;
    void visit(CreateIndexStm &s) override;
    void visit(DropTableStm &s) override;
    void visit(SelectStm &s) override;
    void visit(InsertStm &s) override;
    void visit(DeleteStm &s) override;
    void visit(UpdateStm &s) override;
    void visit(TxStm &s) override;

    void imprimirPrograma(StatementList &lista);

private:
    std::ostream &out_;
};

#endif
