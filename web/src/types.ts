export type Cell = string | number | null;

/** Un nodo del plan. Los detalles del motor llegan planos junto a estos campos. */
export interface PlanStep {
  operacion: string;
  /** etiqueta al estilo de PostgreSQL: Seq Scan, Index Scan, Sort… */
  nodo?: string;
  relacion?: string;
  /** índice usado, vacío si el acceso no usa ninguno */
  indice?: string;
  /** columna sobre la que se aplica el índice o el filtro */
  columna_indice?: string;
  cond?: string;
  /** 0 es la raíz; el número crece hacia el acceso a disco */
  nivel?: number;
  costo?: number;
  filas_estimadas?: number;
  /** -1 cuando el nodo no se ejecutó (EXPLAIN sin ANALYZE) */
  filas_reales?: number;
  nodo_ms?: number;
  nodo_paginas?: number;
  nodo_paginas_escritas?: number;
  [key: string]: string | number | undefined;
}

export interface Result {
  ok: boolean;
  error?: string;
  tipo?: string;
  mensaje?: string;
  afectadas?: number;
  tiempo_ms?: number;
  planificacion_ms?: number;
  /** el plan trae medidas reales (EXPLAIN ANALYZE o una ejecución normal) */
  analizado?: boolean;
  columnas?: string[];
  filas?: Cell[][];
  plan?: PlanStep[];
}
export interface Table {
  nombre: string;
  organizacion: string;
  clave: string;
  archivo: string;
  columnas: Array<{ nombre: string; tipo: string; tam: number; pk: boolean }>;
  indices: Array<{
    nombre: string;
    columna: string;
    tipo: string;
    bytes?: number | null;
  }>;
  estadisticas: Record<string, Cell>;
}
export interface Experiment {
  nombre: string;
  formato?: "csv" | "json";
  columnas: string[];
  filas: Record<string, string>[];
}
