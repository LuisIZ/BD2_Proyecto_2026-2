export async function request<T>(path: string, body?: unknown): Promise<T> {
  let response: Response;
  try {
    response = await fetch(
      `/api/${path}`,
      body === undefined
        ? undefined
        : {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(body),
          },
    );
  } catch {
    throw new Error(
      "No se pudo conectar con la API. Comprueba que npm run dev siga abierto.",
    );
  }
  const data = await response.json().catch(() => null);
  if (!response.ok || data === null) {
    const detail = data?.detail;
    throw new Error(
      typeof detail === "string"
        ? detail
        : Array.isArray(detail)
          ? detail.map((item: { msg: string }) => item.msg).join(". ")
          : "La API no está disponible. Si acaba de iniciar, espera a que termine de compilar el motor y pulsa Actualizar.",
    );
  }
  return data as T;
}
