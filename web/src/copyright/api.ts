import ejemplo from "./ejemplo.json";
import type { Analisis } from "./types";

/** Lo mismo que acepta /api/multimedia/subir. */
export const AUDIO_ACEPTADO = /\.(wav|mp3|flac|ogg)$/i;

/**
 * Manda el audio crudo a POST /api/copyright/analizar, como uploadFile de la vista Consultas.
 * Si la API todavía no tiene la ruta (#93), devuelve ejemplo.json y lo avisa.
 */
export async function analizarAudio(
  archivo: File,
): Promise<{ datos: Analisis; ejemplo: boolean }> {
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
  // FastAPI responde {"detail": "Not Found"} cuando la ruta no existe: #93 aún no está
  if (response.status === 404 && data?.detail === "Not Found")
    return {
      datos: { ...(ejemplo as Analisis), nombre: archivo.name },
      ejemplo: true,
    };
  if (!response.ok || data === null)
    throw new Error(
      typeof data?.detail === "string"
        ? data.detail
        : `La API de copyright respondió ${response.status}.`,
    );
  return { datos: data as Analisis, ejemplo: false };
}
