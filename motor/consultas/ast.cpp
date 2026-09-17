#include <ostream>
#include "ast.h"

const char *nombreDataType(DataType t)
{
    switch (t)
    {
    case DataType::INT: return "INT";
    case DataType::VARCHAR: return "VARCHAR";
    }
    return "?";
}

const char *nombreIndexKind(IndexKind k)
{
    switch (k)
    {
    case IndexKind::SEQUENTIAL: return "SEQUENTIAL";
    case IndexKind::HEAP: return "HEAP";
    case IndexKind::BTREE: return "BTREE";
    case IndexKind::HASH: return "HASH";
    }
    return "?";
}

const char *nombreBinaryOp(BinaryOp op)
{
    switch (op)
    {
    case BinaryOp::OR: return "OR";
    case BinaryOp::AND: return "AND";
    case BinaryOp::EQ: return "=";
    case BinaryOp::NE: return "<>";
    case BinaryOp::LT: return "<";
    case BinaryOp::LE: return "<=";
    case BinaryOp::GT: return ">";
    case BinaryOp::GE: return ">=";
    case BinaryOp::PLUS: return "+";
    case BinaryOp::MINUS: return "-";
    case BinaryOp::STAR: return "*";
    case BinaryOp::PERCENT: return "%";
    }
    return "?";
}

void NumExp::imprimir(std::ostream &out) const
{
    out << valor;
}

void StrExp::imprimir(std::ostream &out) const
{
    out << '\'';
    for (char c : valor)
    {
        if (c == '\'')
            out << "''";
        else
            out << c;
    }
    out << '\'';
}

void ColumnExp::imprimir(std::ostream &out) const
{
    if (!tabla.empty())
        out << tabla << '.';
    out << columna;
}

void BinaryExp::imprimir(std::ostream &out) const
{
    out << '(';
    izq->imprimir(out);
    out << ' ' << nombreBinaryOp(op) << ' ';
    der->imprimir(out);
    out << ')';
}

void UnaryExp::imprimir(std::ostream &out) const
{
    out << '(';
    if (op == UnaryOp::NEG)
        out << '-';
    else
        out << "NOT ";
    operando->imprimir(out);
    out << ')';
}

void FCallExp::imprimir(std::ostream &out) const
{
    out << nombre << '(';
    if (es_star)
    {
        out << '*';
    }
    else
    {
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
                out << ", ";
            args[i]->imprimir(out);
        }
    }
    out << ')';
}

void BetweenExp::imprimir(std::ostream &out) const
{
    out << '(';
    valor->imprimir(out);
    if (negado)
        out << " NOT";
    out << " BETWEEN ";
    bajo->imprimir(out);
    out << " AND ";
    alto->imprimir(out);
    out << ')';
}

void InExp::imprimir(std::ostream &out) const
{
    out << '(';
    valor->imprimir(out);
    if (negado)
        out << " NOT";
    out << " IN (";
    for (std::size_t i = 0; i < lista.size(); ++i)
    {
        if (i > 0)
            out << ", ";
        lista[i]->imprimir(out);
    }
    out << "))";
}

void LikeExp::imprimir(std::ostream &out) const
{
    out << '(';
    valor->imprimir(out);
    if (negado)
        out << " NOT";
    out << " LIKE ";
    patron->imprimir(out);
    out << ')';
}

void CreateTableStm::accept(StmVisitor &v) { v.visit(*this); }
void CreateIndexStm::accept(StmVisitor &v) { v.visit(*this); }
void DropTableStm::accept(StmVisitor &v) { v.visit(*this); }
void SelectStm::accept(StmVisitor &v) { v.visit(*this); }
void InsertStm::accept(StmVisitor &v) { v.visit(*this); }
void DeleteStm::accept(StmVisitor &v) { v.visit(*this); }
void UpdateStm::accept(StmVisitor &v) { v.visit(*this); }
void TxStm::accept(StmVisitor &v) { v.visit(*this); }

SelectStm::~SelectStm()
{
    for (auto &item : sel_list)
        delete item.expresion;
    for (auto &join : from.joins)
        delete join.condicion;
    delete where;
    for (auto *clave : group_by.claves)
        delete clave;
    delete group_by.having;
    for (auto &item : order_by)
        delete item.expresion;
}

InsertStm::~InsertStm()
{
    for (auto &fila : filas)
        for (auto *e : fila)
            delete e;
}

UpdateStm::~UpdateStm()
{
    for (auto &a : asignaciones)
        delete a.valor;
    delete where;
}

static void imprimirTableRef(std::ostream &out, const TableRef &t)
{
    out << t.tabla;
    if (!t.alias.empty())
        out << " AS " << t.alias;
}

void PrintVisitor::visit(CreateTableStm &s)
{
    out_ << "CREATE TABLE " << s.tabla;
    if (!s.columnas.empty())
    {
        out_ << " (";
        for (std::size_t i = 0; i < s.columnas.size(); ++i)
        {
            if (i > 0)
                out_ << ", ";
            const ColumnaDef &c = s.columnas[i];
            out_ << c.nombre << ' ' << nombreDataType(c.tipo);
            if (c.tipo == DataType::VARCHAR)
                out_ << '(' << c.longitud << ')';
            if (c.primary_key)
                out_ << " PRIMARY KEY";
            if (c.unique)
                out_ << " UNIQUE";
            if (c.tiene_indice)
                out_ << " INDEX " << nombreIndexKind(c.indice_tipo);
        }
        out_ << ')';
    }
    if (s.tiene_archivo)
    {
        out_ << " FROM FILE '" << s.archivo.ruta << '\'';
        if (s.archivo.tiene_indice)
            out_ << " USING INDEX " << nombreIndexKind(s.archivo.indice_tipo)
                 << " (" << s.archivo.columna_indice << ')';
    }
}

void PrintVisitor::visit(CreateIndexStm &s)
{
    out_ << "CREATE INDEX " << s.nombre_indice << " ON " << s.tabla
         << " USING " << nombreIndexKind(s.tipo) << " (" << s.columna << ')';
}

void PrintVisitor::visit(DropTableStm &s)
{
    out_ << "DROP TABLE " << s.tabla;
}

void PrintVisitor::visit(SelectStm &s)
{
    out_ << "SELECT ";
    if (s.sel_list.size() == 1 && s.sel_list[0].es_star)
    {
        out_ << '*';
    }
    else
    {
        for (std::size_t i = 0; i < s.sel_list.size(); ++i)
        {
            if (i > 0)
                out_ << ", ";
            const SelItem &item = s.sel_list[i];
            item.expresion->imprimir(out_);
            if (!item.alias.empty())
                out_ << " AS " << item.alias;
        }
    }

    out_ << " FROM ";
    imprimirTableRef(out_, s.from.base);
    for (const JoinClause &j : s.from.joins)
    {
        out_ << " JOIN ";
        imprimirTableRef(out_, j.tabla);
        out_ << " ON ";
        j.condicion->imprimir(out_);
    }

    if (s.where != nullptr)
    {
        out_ << " WHERE ";
        s.where->imprimir(out_);
    }

    if (s.tiene_group_by)
    {
        out_ << " GROUP BY ";
        for (std::size_t i = 0; i < s.group_by.claves.size(); ++i)
        {
            if (i > 0)
                out_ << ", ";
            s.group_by.claves[i]->imprimir(out_);
        }
        if (s.group_by.having != nullptr)
        {
            out_ << " HAVING ";
            s.group_by.having->imprimir(out_);
        }
    }

    if (!s.order_by.empty())
    {
        out_ << " ORDER BY ";
        for (std::size_t i = 0; i < s.order_by.size(); ++i)
        {
            if (i > 0)
                out_ << ", ";
            s.order_by[i].expresion->imprimir(out_);
            out_ << (s.order_by[i].direccion == Direction::ASC ? " ASC" : " DESC");
        }
    }

    if (s.tiene_limit)
    {
        out_ << " LIMIT " << s.limit.limite;
        if (s.limit.tiene_offset)
            out_ << " OFFSET " << s.limit.offset;
    }
}

void PrintVisitor::visit(InsertStm &s)
{
    out_ << "INSERT INTO " << s.tabla;
    if (!s.columnas.empty())
    {
        out_ << " (";
        for (std::size_t i = 0; i < s.columnas.size(); ++i)
        {
            if (i > 0)
                out_ << ", ";
            out_ << s.columnas[i];
        }
        out_ << ')';
    }
    out_ << " VALUES ";
    for (std::size_t i = 0; i < s.filas.size(); ++i)
    {
        if (i > 0)
            out_ << ", ";
        out_ << '(';
        for (std::size_t j = 0; j < s.filas[i].size(); ++j)
        {
            if (j > 0)
                out_ << ", ";
            s.filas[i][j]->imprimir(out_);
        }
        out_ << ')';
    }
}

void PrintVisitor::visit(DeleteStm &s)
{
    out_ << "DELETE FROM " << s.tabla;
    if (s.where != nullptr)
    {
        out_ << " WHERE ";
        s.where->imprimir(out_);
    }
}

void PrintVisitor::visit(UpdateStm &s)
{
    out_ << "UPDATE " << s.tabla << " SET ";
    for (std::size_t i = 0; i < s.asignaciones.size(); ++i)
    {
        if (i > 0)
            out_ << ", ";
        out_ << s.asignaciones[i].columna << " = ";
        s.asignaciones[i].valor->imprimir(out_);
    }
    if (s.where != nullptr)
    {
        out_ << " WHERE ";
        s.where->imprimir(out_);
    }
}

void PrintVisitor::visit(TxStm &s)
{
    switch (s.kind)
    {
    case TxKind::BEGIN_TX: out_ << "BEGIN TRANSACTION"; break;
    case TxKind::COMMIT_TX: out_ << "COMMIT"; break;
    case TxKind::ROLLBACK_TX: out_ << "ROLLBACK"; break;
    case TxKind::END_TRANSACTION: out_ << "END TRANSACTION"; break;
    }
}

void PrintVisitor::imprimirPrograma(StatementList &lista)
{
    for (std::size_t i = 0; i < lista.sentencias.size(); ++i)
    {
        if (i > 0)
            out_ << ";\n";
        lista.sentencias[i]->accept(*this);
    }
}
