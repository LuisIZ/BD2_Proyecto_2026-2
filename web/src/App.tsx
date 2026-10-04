import { useEffect, useRef, useState } from "react";
import { request } from "./api";
import type { Result, Table } from "./types";
import ImportCsv from "./ImportCsv";
import Experiments from "./Experiments";
import Comparison from "./Comparison";
import Demo from "./Demo";
import Syntax from "./Syntax";
import { Plan, Results } from "./Results";
import { statementAt } from "./sql";

const views = [
  ["consultas", "Consultas"],
  ["demo", "Demo guiada"],
  ["comparar", "Comparar estructuras"],
  ["mediciones", "Mediciones"],
  ["sintaxis", "Sintaxis"],
];

const organizations: Record<string, string> = {
  HEAP: "heap",
  SEQUENTIAL: "secuencial",
  BPLUS: "b+ agrupado",
};

export default function App() {
  const [view, setView] = useState("consultas");
  const [tables, setTables] = useState<Table[]>([]);
  const [selected, setSelected] = useState("");
  const [sql, setSql] = useState("SHOW TABLES;");
  const [results, setResults] = useState<Result[]>([]);
  const [resultIndex, setResultIndex] = useState(0);
  const [execution, setExecution] = useState(0);
  const [busy, setBusy] = useState(false);
  const [connected, setConnected] = useState(false);
  const [db, setDb] = useState("");
  const [error, setError] = useState("");
  const [importing, setImporting] = useState(false);
  const [filter, setFilter] = useState("");
  const [history, setHistory] = useState<string[]>([]);
  const editor = useRef<HTMLTextAreaElement>(null);
  const gutter = useRef<HTMLPreElement>(null);
  const running = useRef(false);
  const table = tables.find((item) => item.nombre === selected) ?? tables[0];
  const name = table?.nombre ?? "organizaciones";
  const pk = table?.clave ?? "Index";
  const text =
    table?.columnas.find((column) => column.tipo !== "INT")?.nombre ??
    "Country";
  const activeResult = results[resultIndex];
  const lines = sql.split("\n").length;

  async function refresh() {
    try {
      const data = await request<{ tablas: Table[] }>("catalogo");
      setTables(data.tablas);
      setConnected(true);
      setError("");
      if (!db) setDb((await request<{ db: string }>("health")).db);
    } catch (error) {
      setConnected(false);
      setError((error as Error).message);
    }
  }
  useEffect(() => {
    void refresh();
  }, []);
  useEffect(() => {
    if (!connected) {
      const timer = setInterval(() => void refresh(), 4000);
      return () => clearInterval(timer);
    }
  }, [connected]);

  function showResults(items: Result[]) {
    setResults(items);
    setResultIndex(Math.max(0, items.length - 1));
    setExecution((value) => value + 1);
  }
  function openSql(text: string) {
    setSql(text);
    setView("consultas");
    setTimeout(() => editor.current?.focus());
  }
  async function execute(mode: "todo" | "sentencia" = "todo") {
    if (running.current) return;
    const input = editor.current;
    const query =
      mode === "sentencia"
        ? (statementAt(sql, input?.selectionStart ?? 0)?.text ?? "")
        : input && input.selectionStart !== input.selectionEnd
          ? sql.slice(input.selectionStart, input.selectionEnd)
          : sql;
    if (!query.trim()) return;
    running.current = true;
    setBusy(true);
    setError("");
    setResults([]);
    try {
      const data = await request<{ resultados: Result[] }>("consultas", {
        sql: query,
      });
      showResults(data.resultados);
      setHistory((previous) =>
        [query, ...previous.filter((item) => item !== query)].slice(0, 10),
      );
      await refresh();
    } catch (error) {
      setError((error as Error).message);
    } finally {
      running.current = false;
      setBusy(false);
    }
  }
  /** Antepone EXPLAIN a la sentencia del cursor y la manda sin tocar el editor. */
  async function explain(analyze: boolean) {
    if (running.current) return;
    const input = editor.current;
    const target =
      statementAt(sql, input?.selectionStart ?? 0)?.text ?? sql.trim();
    const clean = target.trim().replace(/;$/, "");
    if (!clean) return;
    if (/^explain\b/i.test(clean)) {
      await run(clean);
      return;
    }
    await run(`EXPLAIN ${analyze ? "ANALYZE " : ""}${clean};`);
  }

  /** Envía un SQL concreto sin cambiar lo que el usuario escribió. */
  async function run(query: string) {
    running.current = true;
    setBusy(true);
    setError("");
    setResults([]);
    try {
      const data = await request<{ resultados: Result[] }>("consultas", {
        sql: query,
      });
      showResults(data.resultados);
      setHistory((previous) =>
        [query, ...previous.filter((item) => item !== query)].slice(0, 10),
      );
    } catch (error) {
      setError((error as Error).message);
    } finally {
      running.current = false;
      setBusy(false);
    }
  }

  const examples: Record<string, string> = {
    listar: `SELECT * FROM ${name} LIMIT 50;`,
    igualdad: `SELECT * FROM ${name} WHERE ${pk} = 5;`,
    rango: `SELECT * FROM ${name} WHERE ${pk} BETWEEN 10 AND 20;`,
    ordenar: `SELECT * FROM ${name} ORDER BY ${pk} DESC LIMIT 20;`,
    agrupar: `SELECT ${text}, COUNT(*) FROM ${name} GROUP BY ${text};`,
    indice: `CREATE INDEX idx_${name}_${pk} ON ${name} (${pk}) USING BPLUS;`,
    insertar: `INSERT INTO ${name} VALUES (${table ? table.columnas.map((column) => (column.tipo === "INT" ? (column.pk ? "100001" : "1") : "'x'")).join(", ") : "100001, 'id', 'x', 'x', 'x', 'x', 2026, 'x', 1"});`,
    eliminar: `DELETE FROM ${name} WHERE ${pk} = 100001;`,
  };

  return (
    <>
      <header className="topbar">
        <strong className="app-name">motor_sql</strong>
        <nav className="tabs" aria-label="Secciones">
          {views.map(([id, label]) => (
            <button
              key={id}
              aria-current={view === id ? "page" : undefined}
              onClick={() => setView(id)}
            >
              {label}
            </button>
          ))}
        </nav>
        <span
          className={`db-status ${connected ? "" : "offline"}`}
          role="status"
        >
          {connected
            ? `${db || "datos/db"} · ${tables.length} ${tables.length === 1 ? "tabla" : "tablas"}`
            : "sin conexión con la API"}
        </span>
      </header>
      <main>
        {error && (
          <div className="message error" role="alert">
            {error}
            <button onClick={refresh} disabled={busy}>
              Reintentar
            </button>
          </div>
        )}
        <div hidden={view !== "consultas"} className="workspace">
          <aside className="panel catalog" aria-labelledby="files-title">
            <div className="panel-heading">
              <h2 id="files-title">Archivos</h2>
              <div className="panel-actions">
                <button
                  className="text-button"
                  onClick={refresh}
                  disabled={busy}
                >
                  Actualizar
                </button>
                <button
                  onClick={() => setImporting(true)}
                  disabled={!connected || busy}
                >
                  Cargar CSV
                </button>
              </div>
            </div>
            <div className="catalog-body">
              <label className="sr-only" htmlFor="table-search">
                Buscar tabla
              </label>
              <input
                id="table-search"
                type="search"
                placeholder="Buscar tabla"
                value={filter}
                onChange={(event) => setFilter(event.target.value)}
              />
              {!tables.length && <p className="empty-small">No hay tablas.</p>}
              <div className="table-list">
                {tables
                  .filter((item) =>
                    item.nombre.toLowerCase().includes(filter.toLowerCase()),
                  )
                  .map((item) => (
                    <button
                      key={item.nombre}
                      className={
                        table?.nombre === item.nombre ? "selected" : ""
                      }
                      onClick={() => setSelected(item.nombre)}
                    >
                      <strong>{item.nombre}</strong>
                      <small>
                        {organizations[item.organizacion] ?? item.organizacion}{" "}
                        ·{" "}
                        {Number(
                          item.estadisticas.registros ?? 0,
                        ).toLocaleString("es-PE")}{" "}
                        registros
                      </small>
                    </button>
                  ))}
              </div>
              {table && (
                <>
                  <div className="schema-heading">
                    <h3>Estructura</h3>
                    <span>{table.columnas.length} columnas</span>
                  </div>
                  <ul className="schema">
                    {table.columnas.map((column) => (
                      <li key={column.nombre}>
                        <span title={column.nombre}>
                          {column.nombre}
                          {column.pk && <b title="Clave primaria">PK</b>}
                        </span>
                        <small>
                          {column.tipo === "INT"
                            ? "INT"
                            : `VARCHAR(${column.tam})`}
                        </small>
                      </li>
                    ))}
                  </ul>
                  <div className="schema-heading">
                    <h3>Índices</h3>
                  </div>
                  {table.indices.length ? (
                    <ul className="index-list">
                      {table.indices.map((index) => (
                        <li key={index.nombre}>
                          <strong>{index.nombre}</strong>
                          <small>
                            {index.tipo === "HASH"
                              ? "Hash extensible"
                              : "B+ no agrupado"}{" "}
                            · {index.columna}
                          </small>
                        </li>
                      ))}
                    </ul>
                  ) : (
                    <p className="muted small">
                      {table.organizacion === "BPLUS"
                        ? `B+ agrupado sobre ${table.clave}.`
                        : "Sin índices secundarios."}
                    </p>
                  )}
                  <button
                    className="full-width"
                    disabled={busy}
                    onClick={() =>
                      openSql(`SELECT * FROM ${table.nombre} LIMIT 50;`)
                    }
                  >
                    Consultar esta tabla
                  </button>
                  <p className="storage-info">
                    {table.estadisticas.paginas ?? "—"} páginas ·{" "}
                    {table.organizacion}
                    <br />
                    <span title={table.archivo}>{table.archivo}</span>
                  </p>
                </>
              )}
            </div>
          </aside>
          <div className="query-workspace">
            <section
              className="panel editor-panel"
              aria-labelledby="editor-title"
            >
              <div className="panel-heading">
                <h2 id="editor-title">Consulta SQL</h2>
                <div className="panel-actions">
                  <select
                    aria-label="Ejemplos SQL"
                    value=""
                    disabled={busy}
                    onChange={(event) => openSql(examples[event.target.value])}
                  >
                    <option value="" disabled>
                      Ejemplos
                    </option>
                    <option value="listar">Listar registros</option>
                    <option value="igualdad">Buscar por clave</option>
                    <option value="rango">Rango por clave</option>
                    <option value="ordenar">ORDER BY</option>
                    <option value="agrupar">GROUP BY</option>
                    <option value="indice">Crear índice B+ (heap)</option>
                    <option value="insertar">INSERT</option>
                    <option value="eliminar">DELETE</option>
                  </select>
                  <select
                    aria-label="Historial de consultas"
                    value=""
                    disabled={!history.length || busy}
                    onChange={(event) =>
                      setSql(history[Number(event.target.value)])
                    }
                  >
                    <option value="" disabled>
                      Historial
                    </option>
                    {history.map((item, i) => (
                      <option value={i} key={i}>
                        {item.slice(0, 80)}
                      </option>
                    ))}
                  </select>
                </div>
              </div>
              <div className="editor-wrap">
                <pre className="editor-gutter" ref={gutter} aria-hidden="true">
                  {Array.from({ length: lines }, (_, i) => i + 1).join("\n")}
                </pre>
                <textarea
                  ref={editor}
                  aria-label="Editor SQL"
                  spellCheck={false}
                  wrap="off"
                  value={sql}
                  onChange={(event) => setSql(event.target.value)}
                  onScroll={(event) => {
                    if (gutter.current)
                      gutter.current.scrollTop = event.currentTarget.scrollTop;
                  }}
                  onKeyDown={(event) => {
                    if (event.key !== "Enter") return;
                    if (event.ctrlKey || event.metaKey) {
                      event.preventDefault();
                      void execute();
                    } else if (event.shiftKey) {
                      event.preventDefault();
                      void execute("sentencia");
                    }
                  }}
                  placeholder="SELECT * FROM organizaciones LIMIT 50;"
                />
              </div>
              <div className="editor-footer">
                <span>
                  Ctrl + Enter ejecuta todo · Shift + Enter solo la sentencia del
                  cursor
                </span>
                <button
                  className="text-button"
                  onClick={() => setSql("")}
                  disabled={busy || !sql}
                >
                  Limpiar
                </button>
                <button
                  onClick={() => void explain(false)}
                  disabled={busy || !connected || !sql.trim()}
                  title="Muestra el plan sin ejecutar la consulta"
                >
                  EXPLAIN
                </button>
                <button
                  onClick={() => void explain(true)}
                  disabled={busy || !connected || !sql.trim()}
                  title="Ejecuta la consulta y muestra el plan con las medidas reales"
                >
                  EXPLAIN ANALYZE
                </button>
                <button
                  onClick={() => void execute("sentencia")}
                  disabled={busy || !connected || !sql.trim()}
                  title="Ejecuta solo la sentencia donde está el cursor"
                >
                  Ejecutar sentencia
                </button>
                <button
                  className="primary"
                  onClick={() => void execute()}
                  disabled={busy || !connected || !sql.trim()}
                >
                  {busy ? "Ejecutando…" : "Ejecutar"}
                </button>
              </div>
            </section>
            {results.length > 1 && (
              <div
                className="statement-tabs"
                aria-label="Resultados por sentencia"
              >
                {results.map((result, i) => (
                  <button
                    key={i}
                    aria-pressed={i === resultIndex}
                    onClick={() => setResultIndex(i)}
                  >
                    Sentencia {i + 1}
                    {!result.ok ? " · error" : ""}
                  </button>
                ))}
              </div>
            )}
            <Results
              key={`${execution}-${resultIndex}`}
              result={activeResult}
              busy={busy}
            />
            <Plan result={activeResult} />
          </div>
        </div>
        <div hidden={view !== "demo"}>
          <Demo
            active={view === "demo"}
            connected={connected}
            refresh={refresh}
            openSql={openSql}
          />
        </div>
        <div hidden={view !== "comparar"}>
          <Comparison
            tables={tables}
            connected={connected}
            refresh={refresh}
            openSql={openSql}
          />
        </div>
        <div hidden={view !== "mediciones"}>
          {view === "mediciones" && <Experiments />}
        </div>
        <div hidden={view !== "sintaxis"}>
          <Syntax />
        </div>
      </main>
      {importing && (
        <ImportCsv
          close={() => setImporting(false)}
          imported={(items, name) => {
            showResults(items);
            setSelected(name);
            setSql(`SELECT * FROM ${name} LIMIT 50;`);
            void refresh();
          }}
        />
      )}
    </>
  );
}
