import { useState } from "react";
import type { PlanStep } from "./types";

/**
 * Dibuja el plan como el grafo de nodos que muestra PostgreSQL: la raíz arriba,
 * el acceso a disco abajo, y una flecha por cada paso que alimenta a su padre.
 * El padre de un nodo es el anterior con un nivel menos: sin JOIN el árbol es una
 * cadena; con JOIN, los dos lados cuelgan del nodo del join, uno al lado del otro.
 *
 * El ancho de la barra de cada nodo es su parte del tiempo total, para que el
 * paso caro salte a la vista sin leer los números.
 */

const nodeWidth = 260;
const nodeHeight = 64;
const gapY = 34;
const gapX = 24;
const padding = 16;

/** Padre y columna de cada nodo; las hojas van de izquierda a derecha y cada padre al centro de sus hijos. */
function layout(plan: PlanStep[]) {
  const level = (i: number) => plan[i].nivel ?? i;
  const parents = plan.map((_, i) => {
    for (let j = i - 1; j >= 0; j--) if (level(j) === level(i) - 1) return j;
    return undefined;
  });
  const children = plan.map(() => [] as number[]);
  parents.forEach((parent, i) => {
    if (parent !== undefined) children[parent].push(i);
  });
  const column: number[] = [];
  let next = 0;
  const place = (i: number): number => {
    if (!children[i].length) return (column[i] = next++);
    const xs = children[i].map(place);
    return (column[i] = (xs[0] + xs[xs.length - 1]) / 2);
  };
  plan.forEach((_, i) => {
    if (parents[i] === undefined) place(i);
  });
  const levels = plan.map((_, i) => level(i));
  return { parents, column, levels, columns: Math.max(1, next), depth: Math.max(0, ...levels) + 1 };
}

/** Páginas leídas por el nodo, contando las del índice y las de datos. */
export function stepPages(step: PlanStep) {
  if (typeof step.nodo_paginas === "number" && step.nodo_paginas >= 0)
    return step.nodo_paginas;
  const read = ["paginas_leidas", "paginas_indice", "paginas_heap"].reduce(
    (total, key) => total + (Number(step[key] ?? 0) || 0),
    0,
  );
  return read;
}

export function stepTime(step: PlanStep) {
  const ms = Number(step.nodo_ms ?? -1);
  return ms >= 0 ? ms : 0;
}

/** Texto corto que explica qué hace el nodo, para quien no conoce los términos. */
const hints: Record<string, string> = {
  scan_completo: "Lee el archivo entero, página por página",
  busqueda_por_clave: "Baja por la estructura hasta la clave",
  rango_por_clave: "Se posiciona y recorre en orden físico",
  busqueda_por_indice: "Busca en el índice y va al dato",
  rango_por_indice: "Una página de datos por cada fila hallada",
  hash_join: "Arma una tabla hash con la tabla más chica y recorre la otra",
  index_nested_loop_join: "Por cada fila de una tabla, busca sus pares con el índice de la otra",
  busqueda_texto: "Recorre los postings de los términos y ordena por score",
  knn_vectorial: "Mide la distancia a cada vector y guarda los k mejores en un heap",
  knn_vectorial_indice: "Lee por clave los candidatos de FAISS y recalcula la distancia",
  top_k_vectorial: "Ordena por distancia las filas del WHERE y se queda con k",
  knn_espacial: "Baja por el R-Tree hacia los puntos más cercanos",
  rango_espacial: "Revisa solo los nodos del R-Tree que tocan el círculo",
  poligono_espacial: "Revisa solo los nodos del R-Tree que tocan el polígono",
  filtro: "Descarta las filas que no cumplen",
  agrupacion: "Agrupa por partición en memoria y disco",
  ordenamiento: "Ordena con mezcla externa",
  proyeccion: "Se queda con las columnas pedidas",
  limite: "Corta el resultado",
  insertar: "Escribe la fila y actualiza los índices",
  eliminar: "Marca la fila como borrada",
};

function label(step: PlanStep) {
  if (step.nodo) return step.nodo;
  return step.operacion.replaceAll("_", " ");
}

export default function PlanGraph({
  plan,
  analyzed,
}: {
  plan: PlanStep[];
  analyzed: boolean;
}) {
  const [active, setActive] = useState<number>();
  if (!plan.length) return null;

  // el motor ya ordena de raíz a hoja; si no hay niveles, se respeta el orden
  const steps = plan;
  const { parents, column, levels, columns, depth } = layout(steps);
  const height = depth * nodeHeight + (depth - 1) * gapY + padding * 2;
  const width = columns * nodeWidth + (columns - 1) * gapX + padding * 2;
  const left = (i: number) => padding + column[i] * (nodeWidth + gapX);
  const top = (i: number) => padding + levels[i] * (nodeHeight + gapY);
  const totalTime = steps.reduce((sum, step) => sum + stepTime(step), 0);
  const maxPages = Math.max(1, ...steps.map(stepPages));

  return (
    <div className="plan-graph">
      <svg
        viewBox={`0 0 ${width} ${height}`}
        style={{ maxWidth: width }}
        role="img"
        aria-label="Grafo del plan de ejecución"
      >
        <defs>
          <marker
            id="plan-arrow"
            viewBox="0 0 10 10"
            refX="9"
            refY="5"
            markerWidth="6"
            markerHeight="6"
            orient="auto-start-reverse"
          >
            <path d="M0 0 L10 5 L0 10 z" className="plan-arrow-head" />
          </marker>
        </defs>
        {steps.map((step, i) => {
          const x = left(i);
          const y = top(i);
          const parent = parents[i];
          const pages = stepPages(step);
          const ms = stepTime(step);
          const share = totalTime > 0 ? ms / totalTime : 0;
          const rows = Number(step.filas_reales ?? -1);
          const estimated = Number(step.filas_estimadas ?? -1);
          const costly = share > 0.5 || pages >= maxPages * 0.8;
          return (
            <g
              key={i}
              className={`plan-node ${active === i ? "active" : ""} ${costly ? "costly" : ""}`}
              onPointerEnter={() => setActive(i)}
              onPointerLeave={() => setActive(undefined)}
            >
              {parent !== undefined && (
                <line
                  className="plan-edge"
                  x1={x + nodeWidth / 2}
                  x2={left(parent) + nodeWidth / 2}
                  y1={y}
                  y2={top(parent) + nodeHeight}
                  markerEnd="url(#plan-arrow)"
                />
              )}
              <rect
                x={x}
                y={y}
                width={nodeWidth}
                height={nodeHeight}
                rx={10}
                className="plan-box"
              />
              {/* barra de tiempo: cuánto del total se fue en este nodo */}
              {analyzed && totalTime > 0 && (
                <rect
                  x={x}
                  y={y + nodeHeight - 4}
                  width={Math.max(2, nodeWidth * share)}
                  height={4}
                  rx={2}
                  className="plan-share"
                />
              )}
              <text x={x + 12} y={y + 22} className="plan-title">
                {label(step)}
              </text>
              {!!step.indice && (
                <text x={x + 12} y={y + 38} className="plan-sub">
                  using {step.indice}
                  {step.columna_indice ? ` (${step.columna_indice})` : ""}
                </text>
              )}
              {!step.indice && !!step.relacion && (
                <text x={x + 12} y={y + 38} className="plan-sub">
                  on {step.relacion}
                </text>
              )}
              <text x={x + 12} y={y + 54} className="plan-metrics">
                {rows >= 0 ? `${rows.toLocaleString("es-PE")} filas` : null}
                {rows < 0 && estimated >= 0
                  ? `~${estimated.toLocaleString("es-PE")} filas`
                  : null}
                {pages > 0 ? ` · ${pages.toLocaleString("es-PE")} págs` : ""}
                {analyzed && ms > 0
                  ? ` · ${ms.toLocaleString("es-PE", { maximumFractionDigits: 2 })} ms`
                  : ""}
              </text>
            </g>
          );
        })}
      </svg>
      {active !== undefined && <PlanTooltip step={steps[active]} />}
    </div>
  );
}

function PlanTooltip({ step }: { step: PlanStep }) {
  const hint = hints[step.operacion];
  const skip = new Set([
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
  return (
    <div className="plan-tip">
      <strong>{label(step)}</strong>
      {hint && <p>{hint}</p>}
      {!!step.cond && (
        <p className="plan-cond">
          <code>{step.cond}</code>
        </p>
      )}
      <dl>
        {Object.entries(step)
          .filter(([key, value]) => !skip.has(key) && value !== "" && value !== undefined)
          .map(([key, value]) => (
            <div key={key}>
              <dt>{key.replaceAll("_", " ")}</dt>
              <dd>{String(value)}</dd>
            </div>
          ))}
      </dl>
    </div>
  );
}
