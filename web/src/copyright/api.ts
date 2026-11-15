import type { Analisis } from "./types";

/** Lo mismo que acepta /api/multimedia/subir. */
export const AUDIO_ACEPTADO = /\.(wav|mp3|flac|ogg)$/i;

/** Manda el audio crudo a POST /api/copyright/analizar, como uploadFile de la vista Consultas. */
export async function analizarAudio(archivo: File): Promise<Analisis> {
  if (!AUDIO_ACEPTADO.test(archivo.name))
    throw new Error("Sube un audio wav, mp3, flac u ogg.");
  let response: Response;
  try {
    response = await fetch(
      `/api/copyright/analizar?nombre=${encodeURIComponent(archivo.name)}`,
      { method: "POST", body: archivo },
    );
  } catch {
    throw new Error(
      "No se pudo conectar con la API. Comprueba que npm run dev siga abierto.",
    );
  }
  const data = await response.json().catch(() => null);
  if (!response.ok || data === null)
    throw new Error(
      typeof data?.detail === "string"
        ? data.detail
        : `La API de copyright respondió ${response.status}.`,
    );
  return data as Analisis;
}
