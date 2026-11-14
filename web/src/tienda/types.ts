/**
 * Contrato de /api/tienda (#90). ejemplo.json tiene una respuesta real de cada endpoint,
 * armada con los datos de datos/tienda (semilla 7).
 */

/** Lo que muestra una tarjeta del catálogo o de las recomendaciones. */
export interface ProductoResumen {
  id: number;
  nombre: string;
  tipo: string;
  marca: string;
  color: string;
  precio: number;
  imagen: string;
}

/** GET /api/tienda/productos?q=&tipo=&pagina= */
export interface Catalogo {
  q: string;
  tipo: string;
  pagina: number;
  por_pagina: number;
  total: number;
  productos: ProductoResumen[];
  /** los tipos con más productos, para el filtro */
  tipos: { tipo: string; productos: number }[];
  consulta: string;
}

/** GET /api/tienda/productos/{id} */
export interface Producto extends ProductoResumen {
  item_id: string;
  categoria: string;
  descripcion: string;
}

export type Fuente = "texto" | "imagen" | "metadata" | "compras";

/** GET /api/tienda/productos/{id}/recomendaciones?k=10 */
export interface Recomendaciones {
  producto_id: number;
  k: number;
  rrf_k: number;
  tiempo_ms: number;
  /** las 4 listas de candidatos; si una falla, ok = false y se fusionan las demás */
  fuentes: {
    fuente: Fuente;
    consulta: string;
    candidatos: number;
    ok: boolean;
    error: string | null;
  }[];
  avisos: string[];
  recomendaciones: (ProductoResumen & {
    /** suma de 1 / (rrf_k + puesto) en cada lista donde aparece */
    score: number;
    fuentes: Fuente[];
    puestos: Partial<Record<Fuente, number>>;
  })[];
}

/** GET /api/tienda/productos/{id}/sedes?lat=&lon=&k=3 */
export interface SedesCercanas {
  producto_id: number;
  origen: { lat: number; lon: number };
  k: number;
  sedes: {
    id: number;
    nombre: string;
    distrito: string;
    lat: number;
    lon: number;
    distancia_m: number;
    stock: number;
  }[];
  consultas: string[];
}
