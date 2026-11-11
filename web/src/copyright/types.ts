/**
 * Contrato de POST /api/copyright/analizar?nombre= (#93). ejemplo.json tiene una respuesta
 * con canciones de GTZAN (datos/audios_1k.csv). Tiempos en segundos; similitud en %.
 */

/** Un tramo de la base de originales (tabla segmentos) parecido a un tramo de la subida. */
export interface Coincidencia {
  segmento_id: number;
  cancion_id: number;
  archivo: string;
  genero: string;
  /** dónde empieza y termina el tramo dentro de la canción original */
  inicio: number;
  fin: number;
  /** SIMILARITY_SCORE() con coseno, en % */
  similitud: number;
}

/** Un tramo de 10 s de la canción subida, con sus k = 5 tramos más parecidos. */
export interface Tramo {
  indice: number;
  inicio: number;
  fin: number;
  /** ordenadas de mayor a menor similitud */
  coincidencias: Coincidencia[];
}

export interface CancionDetectada {
  cancion_id: number;
  archivo: string;
  genero: string;
  /** tramos de la subida cuyo mejor resultado es esta canción con similitud >= umbral */
  tramos: number;
  /** similitud promedio de esos tramos, en % */
  porcentaje: number;
  /** true si tiene al menos minimo_tramos tramos */
  coincide: boolean;
}

export interface Analisis {
  nombre: string;
  duracion: number;
  duracion_tramo: number;
  hop: number;
  umbral: number;
  minimo_tramos: number;
  veredicto: "posible uso no autorizado" | "sin coincidencias";
  tramos: Tramo[];
  canciones: CancionDetectada[];
  consultas: string[];
  tiempo_ms: number;
}
