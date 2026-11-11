import { useState } from "react";
import type { Analisis, Tramo } from "./types";

/** Un color por canción detectada, en el orden de la lista. */
export const COLORES = ["#c2255c", "#2a78d6", "#b26b00", "#287a4b", "#7048e8", "#0c8599"];
export const SIN_COINCIDENCIA = "#9aa59e";

export const segundos = (valor: number) => {
  const total = Math.round(valor);
  return `${Math.floor(total / 60)}:${String(total % 60).padStart(2, "0")}`;
};

/** Nombre corto de un clip de GTZAN: "pop/pop.00011.wav" -> "pop.00011". */
export const nombreCancion = (archivo: string) =>
  archivo.split("/").pop()!.replace(/\.\w+$/, "");

/** La mejor coincidencia del tramo si supera el umbral; si no, ninguna. */
export function mejorCoincidencia(tramo: Tramo, umbral: number) {
  const mejor = tramo.coincidencias[0];
  return mejor && mejor.similitud >= umbral ? mejor : undefined;
}

/** Opacidad según la similitud: 50 % o menos se ve tenue, 100 % se ve lleno. */
const intensidad = (similitud: number) =>
  0.3 + 0.7 * Math.min(1, Math.max(0, (similitud - 50) / 50));

export default function Timeline({
  analisis,
  colores,
  actual,
  reproducirDesde,
}: {
  analisis: Analisis;
  /** cancion_id -> color */
  colores: Map<number, string>;
  /** segundo que está sonando, para la línea de reproducción */
  actual: number;
  reproducirDesde: (segundo: number) => void;
}) {
  const [encima, setEncima] = useState<number>();
  const { duracion, umbral } = analisis;
  const posicion = (segundo: number) => `${(Math.min(segundo, duracion) / duracion) * 100}%`;
  const marcas = Array.from(
    { length: Math.floor(duracion / analisis.hop) + 1 },
    (_, i) => i * analisis.hop,
  );
  const elegido = analisis.tramos.find((tramo) => tramo.indice === encima);
  const detalle = elegido && elegido.coincidencias[0];

  return (
    <div className="timeline">
      <div className="timeline-pista" aria-label="Línea de tiempo de la canción subida">
        {analisis.tramos.map((tramo) => {
          const mejor = mejorCoincidencia(tramo, umbral);
          const similitud = tramo.coincidencias[0]?.similitud ?? 0;
          const color = mejor ? (colores.get(mejor.cancion_id) ?? SIN_COINCIDENCIA) : SIN_COINCIDENCIA;
          return (
            <button
              key={tramo.indice}
              className={`timeline-tramo carril-${tramo.indice % 2}`}
              style={{
                left: posicion(tramo.inicio),
                width: `calc(${posicion(tramo.fin)} - ${posicion(tramo.inicio)})`,
                background: color,
                opacity: intensidad(similitud),
              }}
              aria-label={`Tramo ${segundos(tramo.inicio)}–${segundos(tramo.fin)}: ${
                mejor
                  ? `${nombreCancion(mejor.archivo)}, ${mejor.similitud} %`
                  : "sin coincidencias sobre el umbral"
              }`}
              onMouseEnter={() => setEncima(tramo.indice)}
              onMouseLeave={() => setEncima(undefined)}
              onFocus={() => setEncima(tramo.indice)}
              onBlur={() => setEncima(undefined)}
              onClick={() => reproducirDesde(tramo.inicio)}
            />
          );
        })}
        <span
          className="timeline-cursor"
          style={{ left: posicion(actual) }}
          aria-hidden="true"
        />
      </div>
      <div className="timeline-eje" aria-hidden="true">
        {marcas.map((marca) => (
          <span key={marca} style={{ left: posicion(marca) }}>
            {segundos(marca)}
          </span>
        ))}
      </div>
      <div className="timeline-detalle" role="status">
        {elegido && detalle ? (
          <>
            <strong>
              Tramo {segundos(elegido.inicio)}–{segundos(elegido.fin)}
            </strong>
            {" · "}
            {mejorCoincidencia(elegido, umbral) ? "" : "por debajo del umbral · "}
            {nombreCancion(detalle.archivo)} ({detalle.genero}), desde el{" "}
            {segundos(detalle.inicio)} del original · {detalle.similitud} %
          </>
        ) : (
          <span className="muted">
            Pasa el mouse sobre un tramo para ver su coincidencia; un clic lo reproduce.
          </span>
        )}
      </div>
    </div>
  );
}
