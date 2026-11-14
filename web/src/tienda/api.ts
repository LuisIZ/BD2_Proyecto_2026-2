import ejemplo from "./ejemplo.json";
import type { Catalogo, Producto, Recomendaciones, SedesCercanas } from "./types";

/** La respuesta y si salió de ejemplo.json porque la API todavía no tiene /api/tienda. */
export interface Respuesta<T> {
  datos: T;
  ejemplo: boolean;
}

async function pedir<T>(ruta: string, respaldo: T): Promise<Respuesta<T>> {
  let response: Response;
  try {
    response = await fetch(`/api/tienda/${ruta}`);
  } catch {
    throw new Error(
      "No se pudo conectar con la API. Comprueba que npm run dev siga abierto.",
    );
  }
  const data = await response.json().catch(() => null);
  // FastAPI responde {"detail": "Not Found"} cuando la ruta no existe: #90 aún no está
  if (response.status === 404 && data?.detail === "Not Found")
    return { datos: respaldo, ejemplo: true };
  if (!response.ok || data === null)
    throw new Error(
      typeof data?.detail === "string"
        ? data.detail
        : `La API de la tienda respondió ${response.status}.`,
    );
  return { datos: data as T, ejemplo: false };
}

export function buscarProductos(q: string, tipo: string, pagina: number) {
  const params = new URLSearchParams({ q, tipo, pagina: String(pagina) });
  return pedir<Catalogo>(`productos?${params}`, ejemplo.productos);
}

export function obtenerProducto(id: number) {
  return pedir<Producto>(`productos/${id}`, ejemplo.producto);
}

export function obtenerRecomendaciones(id: number, k = 10) {
  return pedir<Recomendaciones>(
    `productos/${id}/recomendaciones?k=${k}`,
    ejemplo.recomendaciones as Recomendaciones,
  );
}

export function obtenerSedes(id: number, lat: number, lon: number, k = 3) {
  const params = new URLSearchParams({
    lat: String(lat),
    lon: String(lon),
    k: String(k),
  });
  return pedir<SedesCercanas>(`productos/${id}/sedes?${params}`, ejemplo.sedes);
}

export const precio = (valor: number) =>
  valor.toLocaleString("es-PE", {
    style: "currency",
    currency: "PEN",
    minimumFractionDigits: 2,
  });
