/**
 * Respuesta de POST /api/copyright/analizar?nombre= (api/copyright.py, #93; ver docs/copyright.md).
 * Tiempos en segundos; similitud y porcentaje en %.
 */

/** Un tramo de la base de originales (tabla segmentos) parecido a un tramo de la subida. */
export interface Coincidencia {
  cancion_id: number;
  archivo: string;
  genero: string;
  /** dónde empieza y termina el tramo dentro de la canción original */
  inicio_original: number;
  fin_original: number;
  /** SIMILARITY_SCORE() con coseno, en % */
  similitud: number;
  supera_umbral: boolean;
}

/** Un tramo de 10 s de la canción subida, con sus k = 5 tramos más parecidos. */
export interface Tramo {
  inicio: number;
  fin: number;
  /** de mayor a menor similitud; vacía si el tramo es silencio */
  coincidencias: Coincidencia[];
}

export interface CancionDetectada {
  cancion_id: number;
  archivo: string;
  genero: string;
  /** tramos de la subida donde aparece por encima del umbral */
  tramos: number;
  /** parte de los tramos de la subida donde aparece, en % */
  porcentaje: number;
  similitud_maxima: number;
}

export interface Analisis {
  /** nombre con que se guardó la subida: /api/multimedia/subidas/{archivo} */
  archivo: string;
  duracion: number;
  umbral: number;
  veredicto: "posible_copyright" | "sin_coincidencias";
  tramos: Tramo[];
  /** solo las que aparecen en al menos 2 tramos */
  canciones: CancionDetectada[];
  consultas: string[];
}

/** Lo que exige api/copyright.py (TRAMOS_MINIMOS) para reportar una canción. */
export const TRAMOS_MINIMOS = 2;
