#include "parser_sql.h"

#include <cctype>
#include <stdexcept>

namespace motor {
namespace sql {

namespace {

enum class TipoToken { IDENT, NUMERO, DECIMAL, TEXTO, SIMBOLO, FIN };

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
                ++i_;
                const std::size_t decimales = i_;
                while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
                if (i_ == decimales) throw std::runtime_error("numero mal formado: '" + s_.substr(inicio, i_ - inicio) + "'");
                return {TipoToken::DECIMAL, s_.substr(inicio, i_ - inicio)};
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
        if (c == '(' || c == ')' || c == ',' || c == '*' || c == '=' || c == '<' || c == '>' || c == ';') {
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
        Sentencia s = cuerpo();
        if (actual().tipo == TipoToken::SIMBOLO && actual().texto == ";") avanzar();
        if (actual().tipo != TipoToken::FIN) error("texto de mas al final");
        return s;
    }

private:
    // una sentencia sin el ';' final; EXPLAIN la vuelve a llamar para lo que envuelve
    Sentencia cuerpo() {
        Sentencia s;
        if (es("EXPLAIN")) {
            avanzar();
            s.tipo = TipoSentencia::EXPLAIN;
            s.explain = true;
            if (es("ANALYZE") || es("ANALYSE")) { avanzar(); s.analyze = true; }
            if (actual().tipo == TipoToken::FIN) error("EXPLAIN necesita una sentencia");
            Sentencia interna = cuerpo();
            if (interna.tipo == TipoSentencia::EXPLAIN) error("no se puede anidar EXPLAIN");
            s.tabla = interna.tabla;
            s.explicada = std::make_shared<Sentencia>(std::move(interna));
            return s;
        }
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
        } else if (es("COPY")) {
            // COPY t FROM FILE 'ruta.csv': carga datos en una tabla que ya existe
            avanzar();
            s.tipo = TipoSentencia::COPY_FROM_FILE;
            s.tabla = identificador();
            esperar_palabra("FROM");
            esperar_palabra("FILE");
            if (actual().tipo != TipoToken::TEXTO) error("se esperaba la ruta del archivo entre comillas");
            s.archivo_csv = actual().texto;
            avanzar();
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
        } else if (es("BEGIN") || es("START")) {
            avanzar();
            if (es("TRANSACTION")) avanzar();
            s.tipo = TipoSentencia::BEGIN_TRANSACTION;
        } else if (es("END") || es("COMMIT")) {
            avanzar();
            if (es("TRANSACTION")) avanzar();
            s.tipo = TipoSentencia::COMMIT;
        } else if (es("ROLLBACK")) {
            avanzar();
            if (es("TRANSACTION")) avanzar();
            s.tipo = TipoSentencia::ROLLBACK;
        } else {
            error("sentencia no reconocida");
        }
        return s;
    }

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
    bool es_funcion(const char* nombre) const {
        return es(nombre) && pos_ + 1 < t_.size() && t_[pos_ + 1].tipo == TipoToken::SIMBOLO && t_[pos_ + 1].texto == "(";
    }

    Valor valor() {
        const Token t = actual();
        if (t.tipo == TipoToken::NUMERO) { avanzar(); return Valor::de_entero(std::stoll(t.texto)); }
        if (t.tipo == TipoToken::TEXTO) { avanzar(); return Valor::de_texto(t.texto); }
        if (t.tipo == TipoToken::DECIMAL) error("solo se admiten numeros enteros fuera de POINT");
        if (es_funcion("POINT")) return punto();
        error("se esperaba un valor");
    }

    int microgrados() {
        const Token t = actual();
        if (t.tipo != TipoToken::NUMERO && t.tipo != TipoToken::DECIMAL) error("se esperaba una coordenada");
        const bool negativo = t.texto[0] == '-';
        const std::string sin_signo = t.texto.substr(negativo ? 1 : 0);
        const std::size_t punto = sin_signo.find('.');
        const std::string entera = sin_signo.substr(0, punto);
        std::string decimales = punto == std::string::npos ? "" : sin_signo.substr(punto + 1);
        if (decimales.size() > 6) error("una coordenada admite hasta 6 decimales");
        decimales.append(6 - decimales.size(), '0');
        if (entera.size() > 3 || std::stoll(entera) > 180) error("coordenada fuera de rango");
        avanzar();
        const long long v = std::stoll(entera) * 1000000 + std::stoll(decimales);
        return static_cast<int>(negativo ? -v : v);
    }

    Valor punto() {
        avanzar();
        esperar_simbolo("(");
        const int lat = microgrados();
        esperar_simbolo(",");
        const int lon = microgrados();
        esperar_simbolo(")");
        return Valor::de_punto(lat, lon);
    }

    void distancia(std::string& columna, Valor& centro, std::string& metrica) {
        avanzar();
        esperar_simbolo("(");
        columna = identificador();
        esperar_simbolo(",");
        if (!es_funcion("POINT")) error("se esperaba POINT(lat, lon)");
        centro = punto();
        metrica = "HAVERSINE";
        if (es_simbolo(",")) {
            avanzar();
            if (actual().tipo != TipoToken::TEXTO) error("se esperaba la metrica entre comillas");
            const std::string m = mayusculas(actual().texto);
            if (m == "HAVERSINE" || m == "GEODESICA") metrica = "HAVERSINE";
            else if (m == "EUCLIDIANA") metrica = "EUCLIDIANA";
            else error("metrica desconocida '" + actual().texto + "' (haversine o euclidiana)");
            avanzar();
        }
        esperar_simbolo(")");
    }
    long long numero() {
        if (actual().tipo != TipoToken::NUMERO) error("se esperaba un numero");
        long long n = std::stoll(actual().texto);
        avanzar();
        return n;
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
                } else if (tipo == "POINT") {
                    col.tipo = "POINT";
                } else {
                    error("tipo desconocido '" + tipo + "' (INT, VARCHAR(n) o POINT)");
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
            else if (es("INDEX")) {
                // INDEX (a, b): crea un B+ no agrupado por columna al terminar la carga
                avanzar();
                esperar_simbolo("(");
                while (true) {
                    s.indices.push_back(identificador());
                    if (es_simbolo(",")) { avanzar(); continue; }
                    esperar_simbolo(")");
                    break;
                }
            }
            else error("se esperaba USING, PRIMARY KEY o INDEX");
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
            else if (t == "RTREE") s.indice_tipo = "RTREE";
            else error("tipo de indice desconocido '" + t + "' (BPLUS, HASH o RTREE)");
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
            if (es_funcion("DENTRO")) {
                c.funcion = "DENTRO";
                avanzar();
                esperar_simbolo("(");
                c.columna = identificador();
                esperar_simbolo(",");
                if (!es_funcion("POLYGON")) error("se esperaba POLYGON((lat lon, ...))");
                avanzar();
                esperar_simbolo("(");
                esperar_simbolo("(");
                while (true) {
                    const int lat = microgrados();
                    const int lon = microgrados();
                    c.poligono.push_back(Valor::de_punto(lat, lon));
                    if (es_simbolo(",")) { avanzar(); continue; }
                    break;
                }
                esperar_simbolo(")");
                esperar_simbolo(")");
                esperar_simbolo(")");
                if (c.poligono.size() < 3) error("un poligono necesita al menos 3 vertices");
                s.condiciones.push_back(c);
                if (es("AND")) { avanzar(); continue; }
                break;
            }
            if (es_funcion("DISTANCIA")) {
                c.funcion = "DISTANCIA";
                distancia(c.columna, c.punto, c.metrica);
                if (actual().tipo != TipoToken::SIMBOLO) error("se esperaba un operador");
                const std::string op = actual().texto;
                if (op != "<" && op != "<=" && op != ">" && op != ">=") error("distancia solo se compara con < <= > >=");
                avanzar();
                c.op = op;
                if (actual().tipo != TipoToken::NUMERO) error("la distancia se compara con un entero en metros");
                c.valor = valor();
                s.condiciones.push_back(c);
                if (es("AND")) { avanzar(); continue; }
                break;
            }
            c.columna = identificador();
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
                        item.columna = identificador();
                    }
                    esperar_simbolo(")");
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
        if (es("WHERE")) { avanzar(); where(s); }
        if (es("GROUP")) { avanzar(); esperar_palabra("BY"); s.group_by = identificador(); }
        if (es("ORDER")) {
            avanzar();
            esperar_palabra("BY");
            if (es_funcion("DISTANCIA")) {
                s.order_by_distancia = true;
                distancia(s.order_by, s.order_by_punto, s.order_by_metrica);
            } else {
                s.order_by = identificador();
            }
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
