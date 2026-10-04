import type { Result } from "./types";

export const structures = [
  {
    id: "heap",
    table: "org_heap",
    label: "Heap",
    using: "HEAP",
    color: "#eda100",
  },
  {
    id: "seq",
    table: "org_seq",
    label: "Secuencial",
    using: "SEQUENTIAL",
    color: "#1baf7a",
  },
  {
    id: "bplus",
    table: "org_bplus",
    label: "B+ agrupado",
    using: "BPLUS",
    color: "#2a78d6",
  },
  {
    id: "idx",
    table: "org_idx",
    label: "B+ no agrupado",
    using: "HEAP",
    color: "#eb6834",
  },
] as const;

export type StructureId = (typeof structures)[number]["id"];

export const secondaryIndexes = [
  "CREATE INDEX org_idx_index ON org_idx (Index) USING BPLUS;",
  "CREATE INDEX org_idx_founded ON org_idx (Founded) USING BPLUS;",
];

export function setupStatements(csv: string, existing: string[]) {
  const drops = structures
    .filter((s) => existing.includes(s.table))
    .map((s) => `DROP TABLE ${s.table};`);
  const creates = structures.map(
    (s) => `CREATE TABLE ${s.table} FROM FILE 'datos/${csv}' USING ${s.using};`,
  );
  return {
    drops,
    creates,
    statements: [...drops, ...creates, ...secondaryIndexes],
  };
}

interface Param {
  name: string;
  label: string;
  initial: (rows: number) => number;
}

export interface Case {
  id: string;
  label: string;
  note: string;
  params: Param[];
  sql: (table: string, p: Record<string, number>) => string[];
  compare: "rows" | "ordered" | "first-column" | "changes";
}

export const cases: Case[] = [
  {
    id: "clave",
    label: "Búsqueda por clave",
    note: "El Heap no tiene índice sobre Index y recorre páginas hasta encontrar la clave.",
    params: [{ name: "k", label: "Index", initial: (n) => Math.ceil(n / 2) }],
    sql: (t, p) => [`SELECT * FROM ${t} WHERE Index = ${p.k};`],
    compare: "rows",
  },
  {
    id: "rango",
    label: "Rango por clave",
    note: "El B+ no agrupado lee una página del heap por cada fila del rango.",
    params: [
      { name: "a", label: "desde", initial: (n) => Math.floor(n / 10) },
      {
        name: "b",
        label: "hasta",
        initial: (n) => Math.floor(n / 10) + Math.floor(n * 0.03),
      },
    ],
    sql: (t, p) => [
      `SELECT Index, Name, Country FROM ${t} WHERE Index BETWEEN ${p.a} AND ${p.b};`,
    ],
    compare: "rows",
  },
  {
    id: "secundaria",
    label: "Columna no clave",
    note: "Solo org_idx tiene índice sobre Founded. Las otras tablas recorren todo el archivo.",
    params: [{ name: "y", label: "Founded", initial: () => 2000 }],
    sql: (t, p) => [
      `SELECT Index, Name, Founded FROM ${t} WHERE Founded = ${p.y};`,
    ],
    compare: "rows",
  },
  {
    id: "orden",
    label: "ORDER BY",
    note: "External merge sort: el plan muestra los runs iniciales y las pasadas de mezcla.",
    params: [],
    sql: (t) => [
      `SELECT Name, Index, Country FROM ${t} ORDER BY Name LIMIT 20;`,
    ],
    compare: "first-column",
  },
  {
    id: "grupo",
    label: "GROUP BY",
    note: "Agrupación con hashing externo por particiones.",
    params: [],
    sql: (t) => [
      `SELECT Country, COUNT(*) FROM ${t} GROUP BY Country ORDER BY Country;`,
    ],
    compare: "ordered",
  },
  {
    id: "cambios",
    label: "INSERT y DELETE",
    note: "Inserta una clave libre, la busca, la elimina y la vuelve a buscar. El Heap sin índice revisa todo el archivo para validar la clave.",
    params: [{ name: "k", label: "Index", initial: (n) => n + 1 }],
    sql: (t, p) => [
      `INSERT INTO ${t} VALUES (${p.k}, 'demo', 'Demo SAC', 'demo.pe', 'Peru', 'fila de prueba', 2026, 'Education', 10);`,
      `SELECT Index, Name FROM ${t} WHERE Index = ${p.k};`,
      `DELETE FROM ${t} WHERE Index = ${p.k};`,
      `SELECT Index, Name FROM ${t} WHERE Index = ${p.k};`,
    ],
    compare: "changes",
  },
];

export const accessSteps = [
  "scan_completo",
  "busqueda_por_clave",
  "rango_por_clave",
  "busqueda_por_indice",
  "rango_por_indice",
];

function sumField(results: Result[], fields: string[]) {
  let total = 0;
  for (const result of results)
    for (const step of result.plan ?? [])
      for (const field of fields) total += Number(step[field] ?? 0) || 0;
  return total;
}

export const pagesRead = (results: Result[]) =>
  sumField(results, ["paginas_leidas", "paginas_indice", "paginas_heap"]);

export const pagesWritten = (results: Result[]) =>
  sumField(results, ["paginas_escritas"]);

export const elapsed = (results: Result[]) =>
  results.reduce((total, result) => total + (result.tiempo_ms ?? 0), 0);

export function accessStep(results: Result[]) {
  for (const result of results)
    for (const step of result.plan ?? [])
      if (accessSteps.includes(step.operacion)) return step;
}

export function median(values: number[]) {
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2
    ? sorted[middle]
    : (sorted[middle - 1] + sorted[middle]) / 2;
}

export function signature(results: Result[], mode: Case["compare"]) {
  if (mode === "changes") {
    const [insert, found, removed, missing] = results;
    return JSON.stringify([
      insert?.afectadas,
      found?.filas?.length,
      removed?.afectadas,
      missing?.filas?.length,
    ]);
  }
  return JSON.stringify(
    results.map((result) => {
      const rows = result.filas ?? [];
      if (mode === "first-column") return rows.map((row) => row[0]);
      if (mode === "ordered") return rows;
      return rows.map((row) => JSON.stringify(row)).sort();
    }),
  );
}

export const sweepWidths = [
  10, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000,
];

export const sweepSql = (table: string, width: number) =>
  `SELECT COUNT(*) FROM ${table} WHERE Index BETWEEN 1 AND ${width};`;

export function formatNumber(value: number, decimals = 0) {
  return value.toLocaleString("es-PE", {
    minimumFractionDigits: decimals,
    maximumFractionDigits: decimals,
  });
}

export function formatBytes(bytes: number) {
  if (bytes < 1024 * 1024) return `${formatNumber(bytes / 1024, 0)} KB`;
  return `${formatNumber(bytes / 1024 / 1024, 1)} MB`;
}

export function downloadCsv(name: string, rows: (string | number)[][]) {
  const escape = (value: unknown) =>
    `"${String(value ?? "").replaceAll('"', '""')}"`;
  const text = rows.map((row) => row.map(escape).join(",")).join("\r\n");
  const url = URL.createObjectURL(
    new Blob(["\ufeff", text], { type: "text/csv;charset=utf-8" }),
  );
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = name;
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
