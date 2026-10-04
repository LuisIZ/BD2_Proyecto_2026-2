import { useState } from "react";
import type { Result } from "./types";
import PlanGraph, { stepPages } from "./PlanGraph";

export function Results({ result, busy }: { result?: Result; busy: boolean }) {
  const [page, setPage] = useState(0);
  const rows = result?.filas ?? [];
  const columns = result?.columnas ?? [];
  const pages = Math.max(1, Math.ceil(rows.length / 50));
  const current = Math.min(page, pages - 1);

  function download() {
    const escape = (value: unknown) =>
      `"${String(value ?? "").replaceAll('"', '""')}"`;
    const text = [columns, ...rows]
      .map((row) => row.map(escape).join(","))
      .join("\r\n");
    const url = URL.createObjectURL(
      new Blob(["\ufeff", text], { type: "text/csv;charset=utf-8" }),
    );
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = "resultados.csv";
    anchor.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  return (
    <section
      className="panel results"
      aria-labelledby="results-title"
      aria-busy={busy}
    >
      <div className="panel-heading">
        <h2 id="results-title">Resultados</h2>
        <button onClick={download} disabled={!rows.length || busy}>
          Exportar CSV
        </button>
      </div>
      {!result ? (
        <p className="empty-small">Sin resultados.</p>
      ) : !result.ok ? (
        <div className="message error" role="alert">
          <strong>No se ejecutó la sentencia</strong>
          <p>{result.error}</p>
        </div>
      ) : (
        <>
          <div className="result-summary" role="status">
            <span>{result.mensaje || `${rows.length} filas`}</span>
            <span>
              {(result.tiempo_ms ?? 0).toLocaleString("es-PE", {
                maximumFractionDigits: 3,
              })}{" "}
              ms
            </span>
          </div>
          {columns.length > 0 ? (
            <div className="table-scroll">
              <table>
                <caption className="sr-only">
                  Resultados de la consulta SQL
                </caption>
                <thead>
                  <tr>
                    <th scope="col" className="row-number">
                      #
                    </th>
                    {columns.map((col, i) => (
                      <th scope="col" key={i}>
                        {col}
                      </th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {rows
                    .slice(current * 50, (current + 1) * 50)
                    .map((row, i) => (
                      <tr key={i}>
                        <td className="row-number">{current * 50 + i + 1}</td>
                        {row.map((cell, j) => (
                          <td
                            key={j}
                            className={
                              typeof cell === "number" ? "numeric" : ""
                            }
                            title={String(cell ?? "")}
                          >
                            {cell ?? "NULL"}
                          </td>
                        ))}
                      </tr>
                    ))}
                </tbody>
              </table>
              {!rows.length && (
                <p className="empty-small">La consulta no devolvió filas.</p>
              )}
            </div>
          ) : (
            <p className="empty-small">
              Operación completada. {result.afectadas ?? 0} registros afectados.
            </p>
          )}
          {!!rows.length && (
            <div className="pagination">
              <span>
                {current * 50 + 1}–{Math.min((current + 1) * 50, rows.length)}{" "}
                de {rows.length.toLocaleString("es-PE")} filas
              </span>
              <div>
                <button
                  disabled={current === 0}
                  onClick={() => setPage(current - 1)}
                >
                  Anterior
                </button>
                <span>
                  {current + 1} / {pages}
                </span>
                <button
                  disabled={current + 1 >= pages}
                  onClick={() => setPage(current + 1)}
                >
                  Siguiente
                </button>
              </div>
            </div>
          )}
        </>
      )}
    </section>
  );
}

export const labels: Record<string, string> = {
  scan_completo: "Lectura completa",
  busqueda_por_indice: "Búsqueda por índice",
  rango_por_indice: "Rango por índice",
  busqueda_por_clave: "Búsqueda por clave",
  rango_por_clave: "Rango por clave",
  filtro: "Filtro",
  agrupacion: "Agrupación",
  ordenamiento: "Ordenamiento",
  proyeccion: "Selección de columnas",
  limite: "Límite de filas",
  crear_tabla: "Creación de tabla",
  leer_csv: "Lectura del CSV",
  carga_masiva: "Carga masiva",
  insercion_secuencial: "Inserción de registros",
  insercion_una_a_una: "Inserción fila a fila",
  construir_indice: "Construcción del índice",
  insertar: "Inserción",
  eliminar: "Eliminación",
  leer_catalogo: "Lectura del catálogo",
  eliminar_archivos: "Borrado de archivos",
};

/** campos que el motor manda en todos los nodos y que ya se muestran aparte */
const planFields = new Set([
  "operacion",
  "nodo",
  "relacion",
  "indice",
  "columna_indice",
  "cond",
  "nivel",
  "costo",
  "filas_estimadas",
  "filas_reales",
  "nodo_ms",
  "nodo_paginas",
  "nodo_paginas_escritas",
]);

const number = (value: number, decimals = 0) =>
  value.toLocaleString("es-PE", {
    minimumFractionDigits: decimals,
    maximumFractionDigits: decimals,
  });

export function Plan({ result }: { result?: Result }) {
  const [view, setView] = useState<"arbol" | "grafo">("arbol");
  const steps = result?.plan ?? [];
  // los pasos de una consulta vienen anidados; los de un DDL son una secuencia
  const nested = steps.some((step) => (step.nivel ?? 0) > 0);
  const analyzed = !!result?.analizado || steps.some((s) => Number(s.filas_reales ?? -1) >= 0);

  return (
    <section className="panel plan" aria-labelledby="plan-title">
      <div className="panel-heading">
        <h2 id="plan-title">Plan de ejecución</h2>
        {steps.length > 0 && (
          <div className="panel-actions">
            {result?.planificacion_ms ? (
              <span className="muted small">
                planificación {number(result.planificacion_ms, 3)} ms
              </span>
            ) : null}
            <div className="segmented" role="group" aria-label="Vista del plan">
              {(["arbol", "grafo"] as const).map((item) => (
                <button
                  key={item}
                  aria-pressed={view === item}
                  onClick={() => setView(item)}
                >
                  {item === "arbol" ? "Árbol" : "Grafo"}
                </button>
              ))}
            </div>
          </div>
        )}
      </div>
      {!steps.length ? (
        <p className="empty-small">
          {result && !result.ok
            ? "No hay un plan disponible para esta sentencia."
            : "Sin plan."}
        </p>
      ) : view === "grafo" ? (
        <PlanGraph plan={steps} analyzed={analyzed} />
      ) : (
        <ol className={`plan-list ${nested ? "nested" : ""}`}>
          {steps.map((step, index) => {
            const level = nested ? (step.nivel ?? 0) : 0;
            const rows = Number(step.filas_reales ?? -1);
            const estimated = Number(step.filas_estimadas ?? -1);
            const cost = Number(step.costo ?? -1);
            const ms = Number(step.nodo_ms ?? -1);
            const pages = stepPages(step);
            return (
              <li key={index} style={{ marginLeft: level * 18 }}>
                <span className="step-number">
                  {nested ? (level === 0 ? "▣" : "↳") : index + 1}
                </span>
                <div>
                  <strong>
                    {step.nodo || labels[step.operacion] || step.operacion}
                    {/* el nombre al estilo de PostgreSQL manda; la traducción
                        queda al lado para quien ve el plan por primera vez */}
                    {step.nodo && labels[step.operacion] ? (
                      <>
                        {" "}
                        <span className="plan-es">
                          {labels[step.operacion]}
                        </span>
                      </>
                    ) : null}
                    {step.indice ? (
                      <>
                        {" "}
                        <span className="plan-using">
                          using <code>{step.indice}</code>
                          {step.columna_indice ? (
                            <>
                              {" "}
                              sobre <code>{step.columna_indice}</code>
                            </>
                          ) : null}
                        </span>
                      </>
                    ) : null}
                    {step.relacion ? (
                      <span className="plan-on"> on {step.relacion}</span>
                    ) : null}
                  </strong>
                  {/* la línea de costos imita a EXPLAIN: estimado y, si se ejecutó, real */}
                  <p className="plan-costs">
                    {cost >= 0 && (
                      <span>
                        cost={number(cost, 2)} rows=
                        {number(Math.max(0, estimated))}
                      </span>
                    )}
                    {rows >= 0 && (
                      <span className="actual">
                        actual{ms >= 0 ? ` time=${number(ms, 3)}` : ""} rows=
                        {number(rows)}
                        {pages > 0 ? ` pages=${number(pages)}` : ""}
                      </span>
                    )}
                  </p>
                  {!!step.cond && (
                    <p className="plan-cond">
                      <span>
                        {step.indice
                          ? "Index Cond"
                          : step.operacion === "ordenamiento"
                            ? "Sort Key"
                            : step.operacion.startsWith("busqueda") ||
                                step.operacion.startsWith("rango")
                              ? "Key Cond"
                              : "Filter"}
                        :
                      </span>{" "}
                      <code>{step.cond}</code>
                    </p>
                  )}
                  <dl>
                    {Object.entries(step)
                      .filter(
                        ([key, value]) =>
                          !planFields.has(key) && value !== "" && value !== undefined,
                      )
                      .map(([key, value]) => (
                        <div key={key}>
                          <dt>{key.replaceAll("_", " ")}</dt>
                          <dd>{String(value)}</dd>
                        </div>
                      ))}
                  </dl>
                </div>
              </li>
            );
          })}
        </ol>
      )}
    </section>
  );
}
