import { useState } from "react";
import type { Cell } from "./types";

/**
 * Búsqueda por similitud (Parte 4): `SIMILAR_TO(...)` (#62) o el k-NN de vectores
 * `ORDER BY distancia(col, VECTOR(...)) LIMIT k` que ya resuelve el motor.
 */
export interface SimilarSearch {
  k?: number;
  metric?: string;
  /** IVF o HNSW; sin USING es el k-NN secuencial */
  index?: string;
  /** alias de SIMILARITY_SCORE() en el SELECT */
  scoreColumn?: string;
  table?: string;
}

export function parseSimilarSearch(sql: string): SimilarSearch | undefined {
  const similar = /\bSIMILAR_TO\s*\(([^)]*)\)/i.exec(sql);
  const knn =
    /ORDER\s+BY\s+distancia\s*\(\s*[\w.]+\s*,\s*VECTOR\s*\([^)]*\)(?:\s*,\s*'([^']*)')?\s*\)(?:\s+ASC)?\s+LIMIT\s+(\d+)/i.exec(
      sql,
    );
  if (!similar && !knn) return;
  const k = similar ? /\bk\s*=\s*(\d+)/i.exec(similar[1])?.[1] : knn?.[2];
  const metric = similar
    ? /\bMETRIC\s*=\s*(\w+)/i.exec(sql)?.[1]
    : (knn?.[1] ?? "euclidiana");
  return {
    k: k ? Number(k) : undefined,
    metric: metric?.toLowerCase(),
    index: /\bUSING\s+(IVF|HNSW)\b/i.exec(sql)?.[1].toUpperCase(),
    scoreColumn: /\bSIMILARITY_SCORE\s*\(\s*\)\s+(?:AS\s+)?(\w+)/i.exec(sql)?.[1],
    table: /\bFROM\s+(\w+)/i.exec(sql)?.[1].toLowerCase(),
  };
}

/** Columnas que usa la galería: id para la miniatura, nombre y score. */
export function galleryColumns(
  columns: string[],
  rows: Cell[][],
  search: SimilarSearch,
) {
  const lower = columns.map((column) => column.toLowerCase());
  const id = lower.indexOf("id");
  const score = lower.findIndex((column) =>
    [search.scoreColumn?.toLowerCase(), "score", "similarity_score", "distancia"].includes(
      column,
    ),
  );
  const named = lower.indexOf("nombre");
  // si no hay "nombre", la primera columna de texto que no sea el vector
  const name =
    named >= 0
      ? named
      : columns.findIndex(
          (_, i) =>
            i !== id &&
            typeof rows[0]?.[i] === "string" &&
            !/^\s*VECTOR\s*\(/i.test(String(rows[0][i])),
        );
  return { id, name, score };
}

function Thumbnail({ id, alt }: { id: Cell; alt: string }) {
  const [failed, setFailed] = useState(false);
  return failed ? (
    <div className="gallery-missing" role="img" aria-label={`Sin imagen: ${alt}`}>
      sin imagen
    </div>
  ) : (
    <img
      src={`/api/imagenes/${encodeURIComponent(String(id))}`}
      alt={alt}
      loading="lazy"
      onError={() => setFailed(true)}
    />
  );
}

export default function Gallery({
  columns,
  rows,
  search,
  offset,
}: {
  columns: string[];
  rows: Cell[][];
  search: SimilarSearch;
  /** posición de la primera fila, para numerar el ranking entre páginas */
  offset: number;
}) {
  const { id, name, score } = galleryColumns(columns, rows, search);
  return (
    <ol className="gallery" aria-label="Resultados por similitud">
      {rows.map((row, i) => {
        const title = name >= 0 ? String(row[name] ?? "") : `id ${row[id]}`;
        return (
          <li key={i} className="gallery-item">
            <span className="gallery-rank">{offset + i + 1}</span>
            {search.table === "canciones" ? (
              <audio
                controls
                controlsList="nodownload noplaybackrate"
                preload="none"
                src={`/api/audios/${row[id]}`}
              />
            ) : (
              <Thumbnail id={row[id]} alt={title} />
            )}
            <p className="gallery-name" title={title}>
              {title}
            </p>
            <p className="gallery-meta">
              <span>id {String(row[id])}</span>
              {score >= 0 && (
                <span title={columns[score]}>
                  {columns[score]}{" "}
                  {Number(row[score]).toLocaleString("es-PE", {
                    maximumFractionDigits: 4,
                  })}
                </span>
              )}
            </p>
          </li>
        );
      })}
    </ol>
  );
}
