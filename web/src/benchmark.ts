import type { Experiment } from "./types";

export interface BenchmarkPoint {
  structure: string;
  operation: string;
  size: number;
  metric: string;
  value: number;
}

const structureFields = [
  "estructura",
  "implementacion",
  "organizacion",
  "structure",
  "implementation",
  "algorithm",
  "algoritmo",
  "metodo",
  "method",
];
const sizeFields = [
  "n",
  "filas",
  "registros",
  "dataset_size",
  "tamano_dataset",
  "tamaño_dataset",
  "cantidad_registros",
];
const operationFields = [
  "operacion",
  "operación",
  "operation",
  "caso",
  "consulta",
  "operation_name",
];
const metadataFields = new Set([
  ...structureFields,
  ...sizeFields,
  ...operationFields,
  "barajado",
  "seed",
  "semilla",
  "modo",
  "tipo",
]);

function findField(columns: string[], candidates: string[]) {
  const fields = new Map(columns.map((column) => [column.toLowerCase(), column]));
  for (const candidate of candidates) {
    const field = fields.get(candidate);
    if (field) return field;
  }
}

function operationFor(metric: string) {
  const name = metric.toLowerCase();
  if (/construcci|construction|build/.test(name)) return "Construcción";
  if (/inserci|insertion|insert/.test(name)) return "Inserción";
  if (/eliminaci|elimination|delete|borrado/.test(name)) return "Eliminación";
  if (/rango|range/.test(name)) return "Rango";
  if (/busqu|búsqu|igualdad|search|lookup|^eq/.test(name)) return "Búsqueda";
  if (/orden|sort|order/.test(name)) return "Ordenamiento";
  if (/scan|recorrido|secuencial/.test(name)) return "Recorrido";
  if (/reorganiz/.test(name)) return "Reorganización";
  if (/bytes|espacio|storage|altura|paginas_totales/.test(name))
    return "Almacenamiento";
  if (/pagina|página|page/.test(name)) return "Lecturas";
  if (/^t_carga|carga|load/.test(name)) return "Carga";
}

export function benchmarkPoints(file: Experiment): BenchmarkPoint[] {
  const structureField = findField(file.columnas, structureFields);
  const sizeField = findField(file.columnas, sizeFields);
  if (!structureField || !sizeField) return [];

  const operationField = findField(file.columnas, operationFields);
  const metricFields = file.columnas.filter(
    (field) => !metadataFields.has(field.toLowerCase()),
  );
  const points: BenchmarkPoint[] = [];

  for (const row of file.filas) {
    const structure = row[structureField]?.trim();
    const size = Number(row[sizeField]);
    if (!structure || !Number.isFinite(size) || size < 0) continue;

    for (const metric of metricFields) {
      const rawValue = row[metric]?.trim();
      if (!rawValue) continue;
      const value = Number(rawValue);
      if (!Number.isFinite(value) || value < 0) continue;

      const operation =
        row[operationField ?? ""]?.trim() || operationFor(metric);
      if (operation)
        points.push({ structure, operation, size, metric, value });
    }
  }
  return points;
}

export function metricUnit(metric: string) {
  const name = metric.toLowerCase();
  if (name.endsWith("_us")) return "microsegundos";
  if (name.endsWith("_ms") || name.includes("tiempo")) return "milisegundos";
  if (name.includes("bytes") || name.includes("espacio")) return "bytes";
  if (name.includes("pag") || name.includes("page")) return "páginas";
  return metric;
}

export function metricLabel(metric: string) {
  return metric.replaceAll("_", " ");
}

export function benchmarkColor(value: string) {
  const palette = [
    "#2a78d6",
    "#eda100",
    "#1baf7a",
    "#eb6834",
    "#9b6bdb",
    "#db5b85",
    "#58a6a6",
  ];
  let hash = 0;
  for (const character of value)
    hash = (hash * 31 + character.charCodeAt(0)) >>> 0;
  return palette[hash % palette.length];
}
