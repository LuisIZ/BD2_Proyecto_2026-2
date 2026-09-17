#include <unordered_map>
#include "parser.h"

Parser::Parser(Scanner &scanner) : scanner_(scanner), previous_(nullptr)
{
    current_ = scanner_.nextToken();
}

Parser::~Parser()
{
    delete current_;
    delete previous_;
}

bool Parser::check(Token::Type t) const
{
    return current_->type == t;
}

bool Parser::isAtEnd() const
{
    return current_->type == Token::END_INPUT;
}

void Parser::advance()
{
    delete previous_;
    previous_ = current_;
    current_ = scanner_.nextToken();
}

bool Parser::match(Token::Type t)
{
    if (!check(t))
        return false;
    advance();
    return true;
}

void Parser::expect(Token::Type t, const char *contexto)
{
    if (check(t))
    {
        advance();
        return;
    }
    error(std::string("se esperaba ") + Token::typeName(t) + " " + contexto,
          Token::typeName(t));
}

std::string Parser::expectId(const char *contexto)
{
    std::string nombre = current_->text;
    expect(Token::ID, contexto);
    return nombre;
}

long long Parser::expectNum(const char *contexto)
{
    long long v = std::stoll(current_->text);
    expect(Token::NUM, contexto);
    return v;
}

void Parser::error(const std::string &mensaje, const char *esperado)
{
    throw ErrorSintaxis(
        mensaje,
        current_->line, current_->column,
        Token::typeName(current_->type),
        esperado);
}

// Program ::= StmList
// StmList ::= Stm (; Stm)*, con ";" final opcional
StatementList *Parser::parseProgram()
{
    auto *lista = new StatementList();
    try
    {
        lista->sentencias.push_back(parseStatement());
        while (match(Token::SEMICOLON))
        {
            if (isAtEnd())
                break;
            lista->sentencias.push_back(parseStatement());
        }
        if (!isAtEnd())
            error("token sobrante despues de la sentencia");
    }
    catch (...)
    {
        delete lista;
        throw;
    }
    return lista;
}

// Stm ::= CreateTableStm | CreateIndexStm | DropTableStm | SelectStm
//       | InsertStm | DeleteStm | UpdateStm | TxStm
Stm *Parser::parseStatement()
{
    switch (current_->type)
    {
    case Token::CREATE:
        advance();
        if (check(Token::TABLE))
            return parseCreateTable();
        if (check(Token::INDEX))
            return parseCreateIndex();
        error("se esperaba TABLE o INDEX despues de CREATE");
    case Token::DROP:
        return parseDropTable();
    case Token::SELECT:
        return parseSelect();
    case Token::INSERT:
        return parseInsert();
    case Token::DELETE_KW:
        return parseDelete();
    case Token::UPDATE:
        return parseUpdate();
    case Token::BEGIN_KW:
    case Token::COMMIT:
    case Token::END_KW:
    case Token::ROLLBACK:
        return parseTx();
    default:
        error("se esperaba una sentencia (CREATE, DROP, SELECT, INSERT, DELETE, UPDATE o control de transaccion)");
    }
}

// CreateTableStm ::= create table id (ColDecList) [FromFile]
//                  | create table id FromFile
// (CREATE ya fue consumido por parseStatement)
CreateTableStm *Parser::parseCreateTable()
{
    expect(Token::TABLE, "en CREATE TABLE");
    auto *stm = new CreateTableStm();
    try
    {
        stm->tabla = expectId("nombre de la tabla en CREATE TABLE");
        if (match(Token::LP))
        {
            stm->columnas.push_back(parseColDec());
            while (match(Token::COMMA))
                stm->columnas.push_back(parseColDec());
            expect(Token::RP, "cierre de la lista de columnas");
            if (check(Token::FROM))
            {
                stm->archivo = parseFromFile();
                stm->tiene_archivo = true;
            }
        }
        else
        {
            stm->archivo = parseFromFile();
            stm->tiene_archivo = true;
        }
    }
    catch (...)
    {
        delete stm;
        throw;
    }
    return stm;
}

// ColDec ::= id Type (Constraint)*
ColumnaDef Parser::parseColDec()
{
    ColumnaDef col;
    col.nombre = expectId("en la definicion de una columna");
    col.tipo = parseType(col.longitud);
    while (check(Token::PRIMARY) || check(Token::UNIQUE) || check(Token::INDEX))
        parseConstraint(col);
    return col;
}

// Type ::= int | varchar(Num)
DataType Parser::parseType(int &longitud)
{
    longitud = 0;
    if (match(Token::INT))
        return DataType::INT;
    if (match(Token::VARCHAR))
    {
        expect(Token::LP, "en VARCHAR(n)");
        longitud = static_cast<int>(expectNum("longitud de VARCHAR"));
        expect(Token::RP, "cierre de VARCHAR(n)");
        return DataType::VARCHAR;
    }
    error("se esperaba un tipo de columna (INT o VARCHAR)");
}

// Constraint ::= primary key | unique | index IndexType
void Parser::parseConstraint(ColumnaDef &col)
{
    if (match(Token::PRIMARY))
    {
        expect(Token::KEY, "en PRIMARY KEY");
        col.primary_key = true;
        return;
    }
    if (match(Token::UNIQUE))
    {
        col.unique = true;
        return;
    }
    if (match(Token::INDEX))
    {
        col.tiene_indice = true;
        col.indice_tipo = parseIndexType();
        return;
    }
    error("se esperaba una restriccion (PRIMARY KEY, UNIQUE o INDEX)");
}

// FromFile ::= from file Str [using index IndexType (id)]
FromFileClause Parser::parseFromFile()
{
    FromFileClause f;
    expect(Token::FROM, "en FROM FILE");
    expect(Token::FILE_KW, "en FROM FILE");
    f.ruta = current_->text;
    expect(Token::STR, "ruta del archivo en FROM FILE");
    if (match(Token::USING))
    {
        expect(Token::INDEX, "en USING INDEX");
        f.tiene_indice = true;
        f.indice_tipo = parseIndexType();
        expect(Token::LP, "en USING INDEX ... (columna)");
        f.columna_indice = expectId("columna del indice en USING INDEX");
        expect(Token::RP, "cierre de USING INDEX (columna)");
    }
    return f;
}

// IndexType ::= sequential | heap | btree | hash
IndexKind Parser::parseIndexType()
{
    if (match(Token::SEQUENTIAL))
        return IndexKind::SEQUENTIAL;
    if (match(Token::HEAP))
        return IndexKind::HEAP;
    if (match(Token::BTREE))
        return IndexKind::BTREE;
    if (match(Token::HASH))
        return IndexKind::HASH;
    error("se esperaba un tipo de indice (SEQUENTIAL, HEAP, BTREE o HASH)");
}

// CreateIndexStm ::= create index id on id using IndexType (id)
// (CREATE ya fue consumido por parseStatement)
CreateIndexStm *Parser::parseCreateIndex()
{
    expect(Token::INDEX, "en CREATE INDEX");
    auto *stm = new CreateIndexStm();
    try
    {
        stm->nombre_indice = expectId("nombre del indice en CREATE INDEX");
        expect(Token::ON, "en CREATE INDEX ... ON ...");
        stm->tabla = expectId("tabla en CREATE INDEX ... ON ...");
        expect(Token::USING, "en CREATE INDEX");
        stm->tipo = parseIndexType();
        expect(Token::LP, "en CREATE INDEX ... USING tipo (columna)");
        stm->columna = expectId("columna del indice en CREATE INDEX");
        expect(Token::RP, "cierre de CREATE INDEX ... (columna)");
    }
    catch (...)
    {
        delete stm;
        throw;
    }
    return stm;
}

// DropTableStm ::= drop table id
DropTableStm *Parser::parseDropTable()
{
    expect(Token::DROP, "en DROP TABLE");
    expect(Token::TABLE, "en DROP TABLE");
    auto *stm = new DropTableStm();
    stm->tabla = expectId("nombre de la tabla en DROP TABLE");
    return stm;
}

// SelectStm ::= select SelList from Source [Where] [GroupBy] [OrderBy] [Limit]
SelectStm *Parser::parseSelect()
{
    expect(Token::SELECT, "en SELECT");
    auto *stm = new SelectStm();
    try
    {
        stm->sel_list = parseSelList();
        expect(Token::FROM, "en SELECT ... FROM ...");
        stm->from = parseSource();
        stm->where = parseWhere();
        if (check(Token::GROUP))
        {
            stm->group_by = parseGroupBy();
            stm->tiene_group_by = true;
        }
        stm->order_by = parseOrderBy();
        if (check(Token::LIMIT))
        {
            stm->limit = parseLimit();
            stm->tiene_limit = true;
        }
    }
    catch (...)
    {
        delete stm;
        throw;
    }
    return stm;
}

// SelList ::= * | SelItem (, SelItem)*
// SelItem ::= CExp [as id]
std::vector<SelItem> Parser::parseSelList()
{
    std::vector<SelItem> lista;
    if (match(Token::STAR))
    {
        SelItem item;
        item.es_star = true;
        lista.push_back(item);
        return lista;
    }

    auto parseItem = [this]()
    {
        SelItem item;
        item.expresion = parseCExp();
        if (match(Token::AS))
            item.alias = expectId("alias en SELECT ... AS ...");
        return item;
    };

    lista.push_back(parseItem());
    while (match(Token::COMMA))
        lista.push_back(parseItem());
    return lista;
}

// Source ::= TableRef (Join)*
FromClause Parser::parseSource()
{
    FromClause f;
    f.base = parseTableRef();
    while (match(Token::JOIN))
    {
        JoinClause j;
        j.tabla = parseTableRef();
        expect(Token::ON, "en JOIN ... ON ...");
        j.condicion = parseCExp();
        f.joins.push_back(j);
    }
    return f;
}

// TableRef ::= id [[as] id]
TableRef Parser::parseTableRef()
{
    TableRef t;
    t.tabla = expectId("nombre de tabla");
    if (match(Token::AS))
        t.alias = expectId("alias despues de AS");
    else if (check(Token::ID))
    {
        t.alias = current_->text;
        advance();
    }
    return t;
}

// Where ::= where CExp
Exp *Parser::parseWhere()
{
    if (match(Token::WHERE))
        return parseCExp();
    return nullptr;
}

// GroupBy ::= group by ExpList [having CExp]
GroupByClause Parser::parseGroupBy()
{
    GroupByClause g;
    expect(Token::GROUP, "en GROUP BY");
    expect(Token::BY, "en GROUP BY");
    g.claves = parseExpList();
    if (match(Token::HAVING))
        g.having = parseCExp();
    return g;
}

// OrderBy ::= order by OrderItem (, OrderItem)*
// OrderItem ::= CExp [Direction]
std::vector<OrderItem> Parser::parseOrderBy()
{
    std::vector<OrderItem> lista;
    if (!match(Token::ORDER))
        return lista;
    expect(Token::BY, "en ORDER BY");

    auto parseItem = [this]()
    {
        OrderItem item;
        item.expresion = parseCExp();
        if (match(Token::ASC))
            item.direccion = Direction::ASC;
        else if (match(Token::DESC))
            item.direccion = Direction::DESC;
        return item;
    };

    lista.push_back(parseItem());
    while (match(Token::COMMA))
        lista.push_back(parseItem());
    return lista;
}

// Limit ::= limit Num [offset Num]
LimitClause Parser::parseLimit()
{
    LimitClause l;
    expect(Token::LIMIT, "en LIMIT");
    l.limite = expectNum("cantidad en LIMIT");
    if (match(Token::OFFSET))
    {
        l.offset = expectNum("cantidad en OFFSET");
        l.tiene_offset = true;
    }
    return l;
}

// InsertStm ::= insert into id [(id (, id)*)] values Row (, Row)*
// Row ::= (ExpList)
InsertStm *Parser::parseInsert()
{
    expect(Token::INSERT, "en INSERT");
    expect(Token::INTO, "en INSERT INTO");
    auto *stm = new InsertStm();
    try
    {
        stm->tabla = expectId("tabla en INSERT INTO");
        if (match(Token::LP))
        {
            stm->columnas.push_back(expectId("columna en INSERT INTO ... (...)"));
            while (match(Token::COMMA))
                stm->columnas.push_back(expectId("columna en INSERT INTO ... (...)"));
            expect(Token::RP, "cierre de la lista de columnas en INSERT");
        }
        expect(Token::VALUES, "en INSERT ... VALUES ...");

        auto parseRow = [this]()
        {
            expect(Token::LP, "apertura de una fila en VALUES");
            std::vector<Exp *> fila = parseExpList();
            expect(Token::RP, "cierre de una fila en VALUES");
            return fila;
        };

        stm->filas.push_back(parseRow());
        while (match(Token::COMMA))
            stm->filas.push_back(parseRow());
    }
    catch (...)
    {
        delete stm;
        throw;
    }
    return stm;
}

// DeleteStm ::= delete from id [Where]
DeleteStm *Parser::parseDelete()
{
    expect(Token::DELETE_KW, "en DELETE");
    expect(Token::FROM, "en DELETE FROM");
    auto *stm = new DeleteStm();
    try
    {
        stm->tabla = expectId("tabla en DELETE FROM");
        stm->where = parseWhere();
    }
    catch (...)
    {
        delete stm;
        throw;
    }
    return stm;
}

// UpdateStm ::= update id set Assign (, Assign)* [Where]
// Assign ::= id = CExp
UpdateStm *Parser::parseUpdate()
{
    expect(Token::UPDATE, "en UPDATE");
    auto *stm = new UpdateStm();
    try
    {
        stm->tabla = expectId("tabla en UPDATE");
        expect(Token::SET, "en UPDATE ... SET ...");

        auto parseAssign = [this]()
        {
            Assign a;
            a.columna = expectId("columna en UPDATE ... SET ...");
            expect(Token::EQ, "en la asignacion de UPDATE ... SET col = ...");
            a.valor = parseCExp();
            return a;
        };

        stm->asignaciones.push_back(parseAssign());
        while (match(Token::COMMA))
            stm->asignaciones.push_back(parseAssign());
        stm->where = parseWhere();
    }
    catch (...)
    {
        delete stm;
        throw;
    }
    return stm;
}

// TxStm ::= begin [transaction] | commit | end transaction | rollback
TxStm *Parser::parseTx()
{
    if (match(Token::BEGIN_KW))
    {
        match(Token::TRANSACTION);
        return new TxStm(TxKind::BEGIN_TX);
    }
    if (match(Token::COMMIT))
        return new TxStm(TxKind::COMMIT_TX);
    if (match(Token::END_KW))
    {
        expect(Token::TRANSACTION, "en END TRANSACTION");
        return new TxStm(TxKind::END_TRANSACTION);
    }
    if (match(Token::ROLLBACK))
        return new TxStm(TxKind::ROLLBACK_TX);
    error("se esperaba BEGIN, COMMIT, END TRANSACTION o ROLLBACK");
}

// CExp -> OrExp -> AndExp -> NotExp -> CompExp -> Exp -> Term -> Factor,
// de menor a mayor precedencia.
std::vector<Exp *> Parser::parseExpList()
{
    std::vector<Exp *> lista;
    try
    {
        lista.push_back(parseCExp());
        while (match(Token::COMMA))
            lista.push_back(parseCExp());
    }
    catch (...)
    {
        for (auto *e : lista)
            delete e;
        throw;
    }
    return lista;
}

Exp *Parser::parseCExp()
{
    if (++profundidad_ > MAX_PROFUNDIDAD)
    {
        profundidad_ = 0;
        error("expresion demasiado anidada (limite: " + std::to_string(MAX_PROFUNDIDAD) + ")");
    }
    Exp *e;
    try
    {
        e = parseOrExp();
    }
    catch (...)
    {
        --profundidad_;
        throw;
    }
    --profundidad_;
    return e;
}

// OrExp ::= AndExp (or AndExp)*
Exp *Parser::parseOrExp()
{
    Exp *izq = parseAndExp();
    while (match(Token::OR))
    {
        Exp *der = parseAndExp();
        izq = new BinaryExp(izq, der, BinaryOp::OR);
    }
    return izq;
}

// AndExp ::= NotExp (and NotExp)*
Exp *Parser::parseAndExp()
{
    Exp *izq = parseNotExp();
    while (match(Token::AND))
    {
        Exp *der = parseNotExp();
        izq = new BinaryExp(izq, der, BinaryOp::AND);
    }
    return izq;
}

// NotExp ::= [not] CompExp
Exp *Parser::parseNotExp()
{
    if (match(Token::NOT))
    {
        Exp *operando = parseCompExp();
        return new UnaryExp(operando, UnaryOp::NOT);
    }
    return parseCompExp();
}

// CompExp ::= Exp [CompTail]
// CompTail ::= CompOp Exp
//            | [not] between Exp and Exp
//            | [not] in (ExpList)
//            | [not] like Exp
Exp *Parser::parseCompExp()
{
    Exp *izq = parseExp();

    bool negado = match(Token::NOT);

    if (match(Token::BETWEEN))
    {
        Exp *bajo = parseExp();
        expect(Token::AND, "en BETWEEN ... AND ...");
        Exp *alto = parseExp();
        return new BetweenExp(izq, bajo, alto, negado);
    }
    if (match(Token::IN))
    {
        expect(Token::LP, "en IN (...)");
        std::vector<Exp *> lista = parseExpList();
        expect(Token::RP, "cierre de IN (...)");
        return new InExp(izq, lista, negado);
    }
    if (match(Token::LIKE))
    {
        Exp *patron = parseExp();
        return new LikeExp(izq, patron, negado);
    }
    if (negado)
        error("se esperaba BETWEEN, IN o LIKE despues de NOT");

    static const std::unordered_map<int, BinaryOp> ops = {
        {Token::EQ, BinaryOp::EQ}, {Token::NE, BinaryOp::NE},
        {Token::LT, BinaryOp::LT}, {Token::LE, BinaryOp::LE},
        {Token::GT, BinaryOp::GT}, {Token::GE, BinaryOp::GE},
    };
    auto it = ops.find(static_cast<int>(current_->type));
    if (it != ops.end())
    {
        advance();
        Exp *der = parseExp();
        return new BinaryExp(izq, der, it->second);
    }
    return izq; // CompTail ausente
}

// Exp ::= Term (( + | - ) Term)*
Exp *Parser::parseExp()
{
    Exp *izq = parseTerm();
    while (check(Token::PLUS) || check(Token::MINUS))
    {
        BinaryOp op = check(Token::PLUS) ? BinaryOp::PLUS : BinaryOp::MINUS;
        advance();
        Exp *der = parseTerm();
        izq = new BinaryExp(izq, der, op);
    }
    return izq;
}

// Term ::= Factor (( * | % ) Factor)*
Exp *Parser::parseTerm()
{
    Exp *izq = parseFactor();
    while (check(Token::STAR) || check(Token::PERCENT))
    {
        BinaryOp op = check(Token::STAR) ? BinaryOp::STAR : BinaryOp::PERCENT;
        advance();
        Exp *der = parseFactor();
        izq = new BinaryExp(izq, der, op);
    }
    return izq;
}

// Factor ::= Column | Num | Str | (CExp) | PrefixOp Factor | id([ArgList]) | id(*)
// Column ::= id [. id]
Exp *Parser::parseFactor()
{
    if (check(Token::NUM))
    {
        long long v = expectNum("numero");
        return new NumExp(v);
    }
    if (check(Token::STR))
    {
        std::string v = current_->text;
        advance();
        return new StrExp(v);
    }
    if (match(Token::LP))
    {
        Exp *e = parseCExp();
        try
        {
            expect(Token::RP, "cierre de parentesis");
        }
        catch (...)
        {
            delete e;
            throw;
        }
        return e;
    }
    if (match(Token::MINUS))
    {
        Exp *operando = parseFactor();
        return new UnaryExp(operando, UnaryOp::NEG);
    }
    if (check(Token::ID))
    {
        std::string nombre = current_->text;
        advance();
        if (match(Token::LP))
        {
            if (match(Token::STAR))
            {
                expect(Token::RP, "cierre de la llamada a funcion");
                return new FCallExp(nombre, {}, true);
            }
            std::vector<Exp *> args;
            if (!check(Token::RP))
                args = parseExpList();
            try
            {
                expect(Token::RP, "cierre de la llamada a funcion");
            }
            catch (...)
            {
                for (auto *a : args)
                    delete a;
                throw;
            }
            return new FCallExp(nombre, args, false);
        }
        if (match(Token::DOT))
        {
            std::string campo = expectId("nombre de columna despues de '.'");
            return new ColumnExp(nombre, campo);
        }
        return new ColumnExp("", nombre);
    }
    error("se esperaba una expresion (columna, numero, cadena, parentesis o llamada a funcion)");
}
