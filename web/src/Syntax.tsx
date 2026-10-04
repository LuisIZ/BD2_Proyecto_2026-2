const statements = `CREATE TABLE t FROM FILE 'datos/organizations-1000.csv'
  USING HEAP | SEQUENTIAL | BPLUS [PRIMARY KEY col] [INDEX (col, ...)];
CREATE TABLE t (id INT PRIMARY KEY, nombre VARCHAR(40)) USING BPLUS;
CREATE INDEX i ON t (col) USING BPLUS;
COPY t FROM FILE 'datos/organizations-1000.csv';   -- carga en una tabla ya creada
DROP TABLE t;

SELECT * | col, ... | COUNT(*) | SUM(col) | AVG(col) | MIN(col) | MAX(col)
  FROM t
  [WHERE col = v AND col BETWEEN a AND b AND col >= v ...]
  [GROUP BY col] [ORDER BY col ASC | DESC] [LIMIT n];

INSERT INTO t VALUES (v1, v2, ...);
DELETE FROM t [WHERE ...];

EXPLAIN SELECT ...;           -- solo el plan, no toca los datos
EXPLAIN ANALYZE SELECT ...;   -- plan + filas, páginas y tiempo de cada nodo

SHOW TABLES;
DESCRIBE t;`;

const explainExample = `EXPLAIN ANALYZE SELECT Name FROM org_idx WHERE Founded = 2000 LIMIT 5;

Limit on org_idx  (rows=5) (actual rows=5)
  -> Projection on org_idx (actual rows=1940)
     Output: Name
    -> Index Scan using org_idx_founded on org_idx  (cost=103.00 rows=100) (actual time=5.458 rows=1940 pages=1962)
       Index Cond: Founded = 2000
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
          <h2>Sentencias</h2>
        </div>
        <pre className="sql-preview">{statements}</pre>
        <p className="panel-note">
          Varias sentencias se separan con <code>;</code>. Los comentarios{" "}
          <code>-- ...</code> se ignoran hasta el fin de la línea. Si
          seleccionas texto en el editor se ejecuta solo esa parte; con{" "}
          <kbd>Shift + Enter</kbd> o <strong>Ejecutar sentencia</strong> se
          ejecuta solo la sentencia donde está el cursor. Los índices B+ no
          agrupados se crean sobre columnas INT de tablas HEAP.
        </p>
      </section>
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
