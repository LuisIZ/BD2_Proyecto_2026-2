type Consulta = [estructura: string, explicacion: string];

const grupos: { titulo: string; consultas: Consulta[] }[] = [
  {
    titulo: "Tablas y carga",
    consultas: [
      [
        `CREATE TABLE <tabla> (<columna> INT PRIMARY KEY, <columna> INT | FLOAT | VARCHAR(<n>) | POINT | VECTOR(<dim>), ...)
  [USING HEAP | SEQUENTIAL | BPLUS];`,
        "Crea una tabla vacía. USING elige cómo se guardan las filas: HEAP en orden de llegada (por defecto), SEQUENTIAL ordenadas por la clave y BPLUS en un B+ agrupado por la clave.",
      ],
      [
        `CREATE TABLE <tabla> FROM FILE '<archivo.csv>' [USING HEAP | SEQUENTIAL | BPLUS]
  [PRIMARY KEY <columna>] [INDEX (<columna>, ...)];`,
        "Crea la tabla deduciendo las columnas del CSV y la carga. INDEX crea un índice B+ no agrupado por cada columna.",
      ],
      [
        `COPY <tabla> FROM FILE '<archivo.csv>';`,
        "Carga un CSV en una tabla que ya existe. Los encabezados deben ser las columnas de la tabla, en el mismo orden.",
      ],
      [`INSERT INTO <tabla> VALUES (<valor>, <valor>, ...);`, "Agrega una fila."],
      [`DELETE FROM <tabla> [WHERE <condición>];`, "Borra las filas que cumplen la condición, o todas."],
      [`DROP TABLE <tabla>;`, "Borra la tabla, sus datos y sus índices."],
      [`SHOW TABLES;`, "Lista las tablas con su organización, clave, filas, páginas e índices."],
      [`DESCRIBE <tabla>;`, "Muestra las columnas, la clave y los índices de la tabla."],
    ],
  },
  {
    titulo: "Índices",
    consultas: [
      [
        `CREATE INDEX <nombre> ON <tabla> (<columna>) [USING BPLUS | HASH | RTREE | SPIMI];`,
        "Crea un índice en disco sobre una columna. Sin USING es BPLUS.",
      ],
      [
        `CREATE INDEX <nombre> ON <tabla> (<columna VECTOR>) USING IVF | HNSW
  [WITH (<parámetro> = <valor>, ...)];`,
        "Crea un índice de FAISS para SIMILAR_TO ... USING IVF | HNSW. Los parámetros que no se escriben toman su valor por defecto.",
      ],
    ],
  },
  {
    titulo: "Relacionales",
    consultas: [
      [
        `SELECT * | <columna>, ... FROM <tabla>
  [WHERE <columna> = | != | <> | < | <= | > | >= <valor> [AND ...]]
  [ORDER BY <columna> [ASC | DESC]] [LIMIT <n>];`,
        "Filtra con condiciones unidas por AND (no hay OR). Usa la clave o un índice B+ o HASH de la columna si existe; si no, recorre la tabla.",
      ],
      [
        `SELECT * FROM <tabla> WHERE <columna> BETWEEN <desde> AND <hasta>;`,
        "Rango cerrado. Usa la clave en SEQUENTIAL o BPLUS, o un índice B+ (el HASH solo sirve para =).",
      ],
      [
        `SELECT <columna>, COUNT(*) | SUM(<columna>) | AVG(<columna>) | MIN(<columna>) | MAX(<columna>) [AS <alias>]
  FROM <tabla> [WHERE ...] GROUP BY <columna> [ORDER BY <alias> [DESC]] [LIMIT <n>];`,
        "Agrupa con external hashing en disco. Sin GROUP BY, los agregados resumen toda la tabla.",
      ],
      [
        `SELECT <x>.<columna>, <y>.<columna>, ... FROM <tabla_a> [AS] <x> [INNER] JOIN <tabla_b> [AS] <y>
  ON <x>.<columna> = <y>.<columna> [WHERE ...] [GROUP BY ...] [ORDER BY ...] [LIMIT <n>];`,
        "Une dos tablas por igualdad. Index nested loop si una tabla tiene índice o clave sobre su columna del ON; si no, hash join.",
      ],
    ],
  },
  {
    titulo: "Espaciales",
    consultas: [
      [
        `SELECT * FROM <tabla> WHERE distancia(<columna POINT>, POINT(<lat>, <lon>) [, 'haversine' | 'euclidiana']) <= <metros>;`,
        "Puntos dentro de un radio, en metros (un entero). Usa el índice RTREE de la columna. La distancia es haversine por defecto.",
      ],
      [
        `SELECT * FROM <tabla> ORDER BY distancia(<columna POINT>, POINT(<lat>, <lon>)) LIMIT <k>;`,
        "Los k puntos más cercanos, con el RTREE.",
      ],
      [
        `SELECT * FROM <tabla> WHERE dentro(<columna POINT>, POLYGON((<lat> <lon>, <lat> <lon>, <lat> <lon>, ...)));`,
        "Puntos dentro de un polígono. El RTREE descarta lo que cae fuera de su rectángulo.",
      ],
    ],
  },
  {
    titulo: "Texto",
    consultas: [
      [
        `SELECT *, SCORE() [AS <alias>] FROM <tabla>
  WHERE [<condición> AND ...] MATCH(<columna VARCHAR>, '<consulta>') USING TF_IDF | BM25
  [ORDER BY <alias> DESC] [LIMIT <n>];`,
        "Búsqueda por palabras con el índice SPIMI de la columna. USING es obligatorio y elige el ranking; SCORE() es la relevancia. MATCH va una sola vez y no se combina con JOIN ni SIMILAR_TO.",
      ],
    ],
  },
  {
    titulo: "Multimedia",
    consultas: [
      [
        `SELECT <columnas>, SIMILARITY_SCORE() [AS <alias>] FROM <tabla>
  WHERE SIMILAR_TO([<columna VECTOR>,] '<imagen o audio>' | VECTOR(<x1>, ..., <xn>) [, k = <n>]) [AND <condición> ...]
  [USING IVF | HNSW] [WITH METRIC = coseno | euclidiana | producto_punto];`,
        "Los k más parecidos (10 por defecto) a una imagen, un audio o un vector. El archivo se busca en las subidas y en el dataset, y se convierte a vector. Con USING busca en el índice de FAISS; sin USING compara contra todas las filas. La métrica por defecto es coseno y debe ser la misma del índice. SIMILARITY_SCORE() es mayor cuanto más parecido.",
      ],
      [
        `SELECT * FROM <tabla> ORDER BY distancia(<columna VECTOR>, VECTOR(<x1>, ..., <xn>)
  [, 'euclidiana' | 'producto_punto' | 'coseno']) LIMIT <k>;`,
        "Los k vectores más cercanos recorriendo la tabla, sin FAISS. La distancia es euclidiana por defecto.",
      ],
      [
        `SELECT * FROM <tabla> WHERE distancia(<columna VECTOR>, VECTOR(<x1>, ..., <xn>) [, '<métrica>']) < <número>;`,
        "Los vectores a menos de cierta distancia.",
      ],
    ],
  },
  {
    titulo: "Transacciones y EXPLAIN",
    consultas: [
      [
        `BEGIN [TRANSACTION];
  <sentencias>
COMMIT; | ROLLBACK;`,
        "COMMIT (o END) guarda los cambios de las sentencias; ROLLBACK los deshace.",
      ],
      [
        `EXPLAIN <sentencia>;
EXPLAIN ANALYZE <sentencia>;`,
        "EXPLAIN muestra el plan sin tocar los datos. ANALYZE además lo ejecuta y añade filas, páginas y tiempo de cada nodo.",
      ],
    ],
  },
];

const indices = [
  ["BPLUS", "INT, VARCHAR", "=, <, >, BETWEEN y el ON de un JOIN", "—"],
  ["HASH", "INT, VARCHAR", "= y el ON de un JOIN", "—"],
  ["RTREE", "POINT", "radio, k vecinos y dentro(...)", "—"],
  ["SPIMI", "VARCHAR", "MATCH", "—"],
  [
    "IVF",
    "VECTOR",
    "SIMILAR_TO ... USING IVF",
    "nlist = 100: regiones (centroides) en que se parte el espacio · nprobe = 8: regiones que se revisan por consulta · metrica = coseno",
  ],
  [
    "HNSW",
    "VECTOR",
    "SIMILAR_TO ... USING HNSW",
    "M = 32: vecinos de cada nodo del grafo · ef_construccion = 40: candidatos al insertar · ef_busqueda = 64: candidatos al buscar · umbral_reconstruccion = 0.3: fracción de borrados que reconstruye el índice · metrica = coseno",
  ],
];

const explainExample = `EXPLAIN ANALYZE SELECT Name FROM org_idx WHERE Founded = 2000 LIMIT 5;

Limit on org_idx  (rows=5) (actual rows=5)
  -> Projection on org_idx (actual rows=1940)
     Output: Name
    -> Index Scan using org_idx_founded on org_idx (Founded)  (cost=103.00 rows=100) (actual time=5.458 rows=1940 pages=1962)
       Index Cond: Founded = 2000
Rows: 5
Planning Time: 0.507 ms
Execution Time: 20.202 ms`;

const access = [
  ["Columna con índice B+ (=)", "busqueda_por_indice", "—", "—"],
  ["Columna con índice B+ (rango)", "rango_por_indice", "—", "—"],
  [
    "Clave primaria (=)",
    "scan_completo",
    "busqueda_por_clave",
    "busqueda_por_clave",
  ],
  [
    "Clave primaria (rango)",
    "scan_completo",
    "rango_por_clave",
    "rango_por_clave",
  ],
  ["MATCH(col, '...') con índice SPIMI", "busqueda_texto", "busqueda_texto", "busqueda_texto"],
  [
    "distancia(POINT) con índice RTREE",
    "knn_espacial / rango_espacial",
    "knn_espacial / rango_espacial",
    "knn_espacial / rango_espacial",
  ],
  [
    "ORDER BY distancia(VECTOR) LIMIT k",
    "knn_vectorial",
    "knn_vectorial",
    "knn_vectorial",
  ],
  [
    "SIMILAR_TO(...) sin USING (con WHERE: + top_k_vectorial)",
    "knn_vectorial",
    "knn_vectorial",
    "knn_vectorial",
  ],
  [
    "SIMILAR_TO(...) USING IVF | HNSW",
    "knn_vectorial_indice",
    "knn_vectorial_indice",
    "knn_vectorial_indice",
  ],
  [
    "Otra columna",
    "scan_completo + filtro",
    "scan_completo + filtro",
    "scan_completo + filtro",
  ],
];

export default function Syntax() {
  return (
    <div className="syntax">
      <section className="panel">
        <div className="panel-heading">
          <h2>Cómo leer la guía</h2>
        </div>
        <p className="panel-note">
          <code>&lt;tabla&gt;</code> es un valor que escribes tú,{" "}
          <code>[ ... ]</code> es opcional y <code>a | b</code> es una cosa u
          otra. Varias sentencias se separan con <code>;</code> y los
          comentarios <code>-- ...</code> se ignoran hasta el fin de la línea.
          Si seleccionas texto en el editor se ejecuta solo esa parte; con{" "}
          <kbd>Shift + Enter</kbd> o <strong>Ejecutar sentencia</strong> se
          ejecuta solo la sentencia donde está el cursor.
        </p>
      </section>
      {grupos.map((grupo) => (
        <section className="panel" key={grupo.titulo}>
          <div className="panel-heading">
            <h2>{grupo.titulo}</h2>
          </div>
          {grupo.consultas.map(([estructura, explicacion]) => (
            <div className="syntax-entry" key={estructura}>
              <pre className="sql-preview">{estructura}</pre>
              <p className="panel-note">{explicacion}</p>
            </div>
          ))}
          {grupo.titulo === "Índices" && (
            <div className="table-scroll">
              <table>
                <thead>
                  <tr>
                    <th>Índice</th>
                    <th>Columna</th>
                    <th>Sirve para</th>
                    <th>Parámetros WITH (por defecto)</th>
                  </tr>
                </thead>
                <tbody>
                  {indices.map(([indice, columna, uso, parametros]) => (
                    <tr key={indice}>
                      <td>
                        <code>{indice}</code>
                      </td>
                      <td>{columna}</td>
                      <td>{uso}</td>
                      <td>{parametros}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </section>
      ))}
      <section className="panel">
        <div className="panel-heading">
          <h2>EXPLAIN y EXPLAIN ANALYZE</h2>
        </div>
        <p className="panel-note">
          <code>EXPLAIN</code> muestra el plan que elegiría el motor sin abrir
          una sola página de datos: el nodo de acceso, qué índice usa y sobre
          qué columna, más el costo en páginas y las filas estimadas.{" "}
          <code>EXPLAIN ANALYZE</code> ejecuta la consulta y añade a cada nodo
          lo que costó de verdad. Se lee de arriba abajo: la raíz es lo último
          que se ejecuta y el nodo más profundo es el acceso a disco.
        </p>
        <pre className="sql-preview">{explainExample}</pre>
        <p className="panel-note">
          <code>cost</code> son páginas estimadas y <code>rows</code> filas
          estimadas, suponiendo claves densas y repartidas parejo.{" "}
          <code>actual</code> es lo medido. Cuando una se aleja mucho de la otra,
          la estimación se quedó corta: aquí el índice sobre <code>Founded</code>{" "}
          esperaba 100 filas y encontró 1 940, y por eso leyó casi 2 000 páginas.
        </p>
      </section>
      <section className="panel">
        <div className="panel-heading">
          <h2>Acceso que elige el planificador</h2>
        </div>
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Condición del WHERE</th>
                <th>HEAP</th>
                <th>SEQUENTIAL</th>
                <th>BPLUS</th>
              </tr>
            </thead>
            <tbody>
              {access.map(([condition, ...cells]) => (
                <tr key={condition}>
                  <td>{condition}</td>
                  {cells.map((cell, i) => (
                    <td key={i}>
                      <code>{cell}</code>
                    </td>
                  ))}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="panel-note">
          Después vienen <code>agrupacion</code> (external hash aggregate),{" "}
          <code>ordenamiento</code> (external merge sort),{" "}
          <code>proyeccion</code> y <code>limite</code>. Cada paso informa las
          páginas que leyó.
        </p>
      </section>
    </div>
  );
}
