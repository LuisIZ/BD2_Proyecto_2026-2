#include "parser_sql.h"

#include <cctype>
#include <stdexcept>

namespace motor {
namespace sql {

namespace {

enum class TipoToken { IDENT, NUMERO, TEXTO, SIMBOLO, FIN };

struct Token {
    TipoToken tipo;
    std::string texto;
};

std::string mayusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

class Lexer {
public:
    explicit Lexer(const std::string& sql) : s_(sql) {}

    std::vector<Token> tokens() {
        std::vector<Token> salida;
        while (true) {
            Token t = siguiente();
            salida.push_back(t);
            if (t.tipo == TipoToken::FIN) return salida;
        }
    }

private:
    Token siguiente() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
        if (i_ >= s_.size()) return {TipoToken::FIN, ""};
        const char c = s_[i_];

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t inicio = i_;
            while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) ++i_;
            return {TipoToken::IDENT, s_.substr(inicio, i_ - inicio)};
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && i_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_ + 1])))) {
            std::size_t inicio = i_;
            ++i_;
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
            if (i_ < s_.size() && s_[i_] == '.') {
                throw std::runtime_error("solo se admiten numeros enteros: '" + s_.substr(inicio, i_ - inicio + 1) + "'");
            }
            return {TipoToken::NUMERO, s_.substr(inicio, i_ - inicio)};
        }
        if (c == '\'' || c == '"') {
            const char comilla = c;
            ++i_;
            std::string texto;
            while (true) {
                if (i_ >= s_.size()) throw std::runtime_error("texto sin cerrar");
                if (s_[i_] == comilla) {
                    if (i_ + 1 < s_.size() && s_[i_ + 1] == comilla) {  // '' escapa la comilla
                        texto.push_back(comilla);
                        i_ += 2;
                        continue;
                    }
                    ++i_;
                    break;
                }
                texto.push_back(s_[i_++]);
            }
            return {TipoToken::TEXTO, texto};
        }
        // símbolos de dos caracteres primero
        if (i_ + 1 < s_.size()) {
            const std::string dos = s_.substr(i_, 2);
            if (dos == "<=" || dos == ">=" || dos == "!=" || dos == "<>") {
                i_ += 2;
                return {TipoToken::SIMBOLO, dos == "<>" ? "!=" : dos};
            }
        }
        if (c == '(' || c == ')' || c == ',' || c == '*' || c == '=' || c == '<' || c == '>' || c == ';' || c == '.') {
            ++i_;
            return {TipoToken::SIMBOLO, std::string(1, c)};
        }
        throw std::runtime_error(std::string("caracter inesperado: '") + c + "'");
    }

    const std::string& s_;
    std::size_t i_ = 0;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : t_(std::move(tokens)) {}

    Sentencia sentencia() {
        Sentencia s;
        if (es("CREATE")) {
            avanzar();
            if (es("TABLE")) { avanzar(); create_table(s); }
            else if (es("INDEX")) { avanzar(); create_index(s); }
            else error("se esperaba TABLE o INDEX");
        } else if (es("DROP")) {
            avanzar();
            esperar_palabra("TABLE");
            s.tipo = TipoSentencia::DROP_TABLE;
            s.tabla = identificador();
        } else if (es("INSERT")) {
            avanzar();
            insert(s);
        } else if (es("DELETE")) {
            avanzar();
            delete_from(s);
        } else if (es("SELECT")) {
            avanzar();
            select(s);
        } else if (es("SHOW")) {
            avanzar();
            esperar_palabra("TABLES");
            s.tipo = TipoSentencia::SHOW_TABLES;
        } else if (es("DESCRIBE") || es("DESC")) {
            avanzar();
            s.tipo = TipoSentencia::DESCRIBE;
            s.tabla = identificador();
        } else {
            error("sentencia no reconocida");
        }
        if (actual().tipo == TipoToken::SIMBOLO && actual().texto == ";") avanzar();
        if (actual().tipo != TipoToken::FIN) error("texto de mas al final");
        return s;
    }

private:
    const Token& actual() const { return t_[pos_]; }
    void avanzar() { if (pos_ + 1 < t_.size()) ++pos_; }

    bool es(const char* palabra) const {
        return actual().tipo == TipoToken::IDENT && mayusculas(actual().texto) == palabra;
    }
    bool es_simbolo(const char* simbolo) const {
        return actual().tipo == TipoToken::SIMBOLO && actual().texto == simbolo;
    }
    [[noreturn]] void error(const std::string& que) const {
        const std::string cerca = actual().tipo == TipoToken::FIN ? "el final" : "'" + actual().texto + "'";
        throw std::runtime_error("error de sintaxis: " + que + " cerca de " + cerca);
    }
    void esperar_palabra(const char* palabra) {
        if (!es(palabra)) error(std::string("se esperaba ") + palabra);
        avanzar();
    }
    void esperar_simbolo(const char* simbolo) {
        if (!es_simbolo(simbolo)) error(std::string("se esperaba '") + simbolo + "'");
        avanzar();
    }
    std::string identificador() {
        if (actual().tipo != TipoToken::IDENT) error("se esperaba un nombre");
        std::string nombre = actual().texto;
        avanzar();
        return nombre;
    }
    Valor valor() {
        const Token t = actual();
        if (t.tipo == TipoToken::NUMERO) { avanzar(); return Valor::de_entero(std::stoll(t.texto)); }
        if (t.tipo == TipoToken::TEXTO) { avanzar(); return Valor::de_texto(t.texto); }
        error("se esperaba un valor");
    }
    long long numero() {
        if (actual().tipo != TipoToken::NUMERO) error("se esperaba un numero");
        long long n = std::stoll(actual().texto);
        avanzar();
        return n;
    }

    // nombre | tabla.nombre
    void nombre_columna(std::string& calificador, std::string& columna) {
        columna = identificador();
        calificador.clear();
        if (es_simbolo(".")) {
            avanzar();
            calificador = columna;
            columna = identificador();
        }
    }

    std::string organizacion() {
        const std::string o = mayusculas(identificador());
        if (o == "HEAP") return "HEAP";
        if (o == "SEQUENTIAL" || o == "SECUENCIAL") return "SEQUENTIAL";
        if (o == "BPLUS" || o == "BPLUS_AGRUPADO" || o == "AGRUPADO" || o == "CLUSTERED") return "BPLUS";
        error("organizacion desconocida '" + o + "' (HEAP, SEQUENTIAL o BPLUS)");
    }

    void create_table(Sentencia& s) {
        s.tabla = identificador();
        if (es("FROM")) {
            avanzar();
            esperar_palabra("FILE");
            if (actual().tipo != TipoToken::TEXTO) error("se esperaba la ruta del archivo entre comillas");
            s.tipo = TipoSentencia::CREATE_TABLE_FROM_FILE;
            s.archivo_csv = actual().texto;
            avanzar();
        } else {
            s.tipo = TipoSentencia::CREATE_TABLE;
            esperar_simbolo("(");
            while (true) {
                ColumnaDef col;
                col.nombre = identificador();
                const std::string tipo = mayusculas(identificador());
                if (tipo == "INT" || tipo == "INTEGER") {
                    col.tipo = "INT";
                } else if (tipo == "VARCHAR" || tipo == "CHAR") {
                    col.tipo = "VARCHAR";
                    esperar_simbolo("(");
                    col.tam = static_cast<int>(numero());
                    esperar_simbolo(")");
                    if (col.tam <= 0) error("VARCHAR necesita un largo positivo");
                } else {
                    error("tipo desconocido '" + tipo + "' (INT o VARCHAR(n))");
                }
                if (es("PRIMARY")) {
                    avanzar();
                    esperar_palabra("KEY");
                    col.pk = true;
                }
                s.columnas.push_back(col);
                if (es_simbolo(",")) { avanzar(); continue; }
                esperar_simbolo(")");
                break;
            }
        }
        while (actual().tipo == TipoToken::IDENT) {
            if (es("USING")) { avanzar(); s.organizacion = organizacion(); }
            else if (es("PRIMARY")) { avanzar(); esperar_palabra("KEY"); s.pk = identificador(); }
            else error("se esperaba USING o PRIMARY KEY");
        }
    }

    void create_index(Sentencia& s) {
        s.tipo = TipoSentencia::CREATE_INDEX;
        s.indice_nombre = identificador();
        esperar_palabra("ON");
        s.tabla = identificador();
        esperar_simbolo("(");
        s.indice_columna = identificador();
        esperar_simbolo(")");
        s.indice_tipo = "BPLUS";
        if (es("USING")) {
            avanzar();
            const std::string t = mayusculas(identificador());
            if (t == "BPLUS" || t == "BTREE") s.indice_tipo = "BPLUS";
            else if (t == "HASH") s.indice_tipo = "HASH";
            else error("tipo de indice desconocido '" + t + "' (BPLUS o HASH)");
        }
    }

    void insert(Sentencia& s) {
        s.tipo = TipoSentencia::INSERT;
        esperar_palabra("INTO");
        s.tabla = identificador();
        esperar_palabra("VALUES");
        esperar_simbolo("(");
        while (true) {
            s.valores.push_back(valor());
            if (es_simbolo(",")) { avanzar(); continue; }
            esperar_simbolo(")");
            break;
        }
    }

    void delete_from(Sentencia& s) {
        s.tipo = TipoSentencia::DELETE_FROM;
        esperar_palabra("FROM");
        s.tabla = identificador();
        if (es("WHERE")) { avanzar(); where(s); }
    }

    void where(Sentencia& s) {
        while (true) {
            Condicion c;
            nombre_columna(c.calificador, c.columna);
            if (es("BETWEEN")) {
                avanzar();
                c.op = "BETWEEN";
                c.valor = valor();
                esperar_palabra("AND");
                c.hasta = valor();
            } else {
                if (actual().tipo != TipoToken::SIMBOLO) error("se esperaba un operador");
                const std::string op = actual().texto;
                if (op != "=" && op != "!=" && op != "<" && op != "<=" && op != ">" && op != ">=") {
                    error("operador desconocido '" + op + "'");
                }
                avanzar();
                c.op = op;
                c.valor = valor();
            }
            s.condiciones.push_back(c);
            if (es("AND")) { avanzar(); continue; }
            break;
        }
    }

    // [INNER] JOIN tabla ON col = col
    void joins(Sentencia& s) {
        while (true) {
            if (es("LEFT") || es("RIGHT") || es("FULL") || es("OUTER") || es("CROSS")) {
                error("por ahora solo se admite INNER JOIN");
            }
            const bool inner_explicito = es("INNER");
            if (inner_explicito) avanzar();
            if (!es("JOIN")) {
                if (inner_explicito) error("se esperaba JOIN despues de INNER");
                return;
            }
            avanzar();
            JoinSpec j;
            j.tabla_derecha = identificador();
            esperar_palabra("ON");
            nombre_columna(j.izq_calificador, j.izq_columna);
            if (!es_simbolo("=")) error("la condicion ON solo admite igualdad entre columnas");
            avanzar();
            nombre_columna(j.der_calificador, j.der_columna);
            if (es("AND")) error("la condicion ON solo admite una igualdad");
            s.joins.push_back(j);
        }
    }

    void select(Sentencia& s) {
        s.tipo = TipoSentencia::SELECT;
        if (es_simbolo("*")) {
            avanzar();
            s.todas_las_columnas = true;
        } else {
            while (true) {
                ItemSelect item;
                const std::string nombre = identificador();
                const std::string may = mayusculas(nombre);
                if ((may == "COUNT" || may == "SUM" || may == "AVG" || may == "MIN" || may == "MAX") && es_simbolo("(")) {
                    avanzar();
                    item.agregado = may;
                    if (es_simbolo("*")) {
                        if (may != "COUNT") error("solo COUNT admite *");
                        avanzar();
                    } else {
                        nombre_columna(item.calificador, item.columna);
                    }
                    esperar_simbolo(")");
                } else if (es_simbolo(".")) {
                    avanzar();
                    item.calificador = nombre;
                    item.columna = identificador();
                } else {
                    item.columna = nombre;
                }
                s.items.push_back(item);
                if (es_simbolo(",")) { avanzar(); continue; }
                break;
            }
        }
        esperar_palabra("FROM");
        s.tabla = identificador();
        joins(s);
        if (es("WHERE")) { avanzar(); where(s); }
        if (es("GROUP")) { avanzar(); esperar_palabra("BY"); nombre_columna(s.group_by_calificador, s.group_by); }
        if (es("ORDER")) {
            avanzar();
            esperar_palabra("BY");
            nombre_columna(s.order_by_calificador, s.order_by);
            if (es("ASC")) avanzar();
            else if (es("DESC")) { avanzar(); s.descendente = true; }
        }
        if (es("LIMIT")) {
            avanzar();
            s.limite = numero();
            if (s.limite < 0) error("LIMIT debe ser positivo");
        }
    }

    std::vector<Token> t_;
    std::size_t pos_ = 0;
};

}  // namespace

Sentencia parsear(const std::string& sql) {
    Lexer lexer(sql);
    Parser parser(lexer.tokens());
    return parser.sentencia();
}

std::vector<std::string> separar_sentencias(const std::string& texto) {
    std::vector<std::string> salida;
    std::string actual;
    char comilla = 0;
    for (const char c : texto) {
        if (comilla) {
            actual.push_back(c);
            if (c == comilla) comilla = 0;
        } else if (c == '\'' || c == '"') {
            comilla = c;
            actual.push_back(c);
        } else if (c == ';') {
            salida.push_back(actual);
            actual.clear();
        } else {
            actual.push_back(c);
        }
    }
    salida.push_back(actual);

    std::vector<std::string> limpias;
    for (std::string& s : salida) {
        const std::size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        const std::size_t b = s.find_last_not_of(" \t\r\n");
        limpias.push_back(s.substr(a, b - a + 1));
    }
    return limpias;
}

}  // namespace sql
}  // namespace motor
