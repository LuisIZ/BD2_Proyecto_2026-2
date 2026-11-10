import { useEffect, useState } from "react";
import { Tarjeta } from "./Catalogo";
import { obtenerRecomendaciones } from "./api";
import type { Fuente, Recomendaciones as Datos } from "./types";

export const NOMBRE_FUENTE: Record<Fuente, string> = {
  texto: "Texto",
  imagen: "Imagen",
  metadata: "Metadata",
  compras: "Compras",
};

export default function Recomendaciones({
  id,
  abrir,
}: {
  id: number;
  abrir: (id: number) => void;
}) {
  const [datos, setDatos] = useState<Datos>();
  const [error, setError] = useState("");

  useEffect(() => {
    let vigente = true;
    setDatos(undefined);
    setError("");
    obtenerRecomendaciones(id)
      .then((respuesta) => vigente && setDatos(respuesta.datos))
      .catch((e: Error) => vigente && setError(e.message));
    return () => {
      vigente = false;
    };
  }, [id]);

  const consultas = Object.fromEntries(
    (datos?.fuentes ?? []).map((fuente) => [fuente.fuente, fuente.consulta]),
  ) as Partial<Record<Fuente, string>>;
  const fallidas = datos?.fuentes.filter((fuente) => !fuente.ok) ?? [];

  return (
    <section className="panel" aria-labelledby="recomendaciones-titulo">
      <div className="panel-heading">
        <h2 id="recomendaciones-titulo">También te puede interesar</h2>
        {datos && (
          <span className="muted small">
            fusión RRF de {datos.fuentes.length - fallidas.length} listas ·{" "}
            {datos.tiempo_ms.toLocaleString("es-PE")} ms
          </span>
        )}
      </div>
      {error && (
        <p className="message error tienda-estado" role="alert">
          {error}
        </p>
      )}
      {!datos && !error && (
        <p className="empty-small" role="status">
          Calculando recomendaciones…
        </p>
      )}
      {[...(datos?.avisos ?? []), ...fallidas.map((f) => `La fuente ${NOMBRE_FUENTE[f.fuente]} falló: ${f.error ?? "sin detalle"}`)].map(
        (aviso) => (
          <p key={aviso} className="panel-note warn">
            {aviso}
          </p>
        ),
      )}
      {datos && (
        <ol className="tienda-grilla" aria-label="Recomendaciones">
          {datos.recomendaciones.map((item, i) => (
            <Tarjeta key={item.id} producto={item} abrir={abrir}>
              <span className="gallery-rank">{i + 1}</span>
              <span className="tienda-fuentes">
                {item.fuentes.map((fuente) => (
                  <span key={fuente} className={`tienda-fuente ${fuente}`}>
                    {NOMBRE_FUENTE[fuente]}
                  </span>
                ))}
              </span>
              <details className="tienda-sql">
                <summary>Por qué aparece</summary>
                <p className="small">
                  score RRF {item.score.toFixed(4)} = suma de 1 / ({datos.rrf_k} + puesto)
                </p>
                <dl className="tienda-puestos">
                  {item.fuentes.map((fuente) => (
                    <div key={fuente}>
                      <dt>
                        {NOMBRE_FUENTE[fuente]} · puesto {item.puestos[fuente]}
                      </dt>
                      <dd>
                        <code>{consultas[fuente]}</code>
                      </dd>
                    </div>
                  ))}
                </dl>
              </details>
            </Tarjeta>
          ))}
        </ol>
      )}
    </section>
  );
}
