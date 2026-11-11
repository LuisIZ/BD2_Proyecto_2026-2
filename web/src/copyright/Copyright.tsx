import { useEffect, useMemo, useRef, useState } from "react";
import Timeline, { COLORES, nombreCancion, segundos } from "./Timeline";
import { analizarAudio } from "./api";
import type { Analisis } from "./types";

/** Vista "Copyright" (#94): sube un audio y muestra qué tramos coinciden con canciones originales. */
export default function Copyright() {
  const entrada = useRef<HTMLInputElement>(null);
  const reproductor = useRef<HTMLAudioElement>(null);
  const [archivo, setArchivo] = useState<File>();
  const [url, setUrl] = useState("");
  const [analisis, setAnalisis] = useState<Analisis>();
  const [ejemplo, setEjemplo] = useState(false);
  const [cargando, setCargando] = useState(false);
  const [error, setError] = useState("");
  const [actual, setActual] = useState(0);
  const [escuchando, setEscuchando] = useState<number>();

  // el audio se reproduce desde el navegador, sin volver a pedirlo a la API
  useEffect(() => {
    if (!archivo) return;
    const enlace = URL.createObjectURL(archivo);
    setUrl(enlace);
    return () => URL.revokeObjectURL(enlace);
  }, [archivo]);

  async function analizar(nuevo: File) {
    setArchivo(nuevo);
    setAnalisis(undefined);
    setError("");
    setActual(0);
    setEscuchando(undefined);
    setCargando(true);
    try {
      const respuesta = await analizarAudio(nuevo);
      setAnalisis(respuesta.datos);
      setEjemplo(respuesta.ejemplo);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setCargando(false);
    }
  }

  function reproducirDesde(segundo: number) {
    const audio = reproductor.current;
    if (!audio) return;
    audio.currentTime = segundo;
    setActual(segundo);
    void audio.play().catch(() => undefined);
  }

  const colores = useMemo(
    () =>
      new Map(
        (analisis?.canciones ?? []).map((cancion, i) => [
          cancion.cancion_id,
          COLORES[i % COLORES.length],
        ]),
      ),
    [analisis],
  );
  const sospechoso = analisis?.veredicto === "posible uso no autorizado";

  return (
    <div className="copyright">
      {ejemplo && (
        <p className="panel-note warn" role="note">
          Datos de ejemplo: la API todavía no tiene /api/copyright/analizar (#93), así que se
          muestra web/src/copyright/ejemplo.json.
        </p>
      )}
      <section className="panel" aria-labelledby="copyright-titulo">
        <div className="panel-heading">
          <h2 id="copyright-titulo">Detección de copyright</h2>
          <div className="panel-actions">
            <input
              ref={entrada}
              type="file"
              hidden
              aria-label="Audio a analizar"
              accept="audio/*,.wav,.mp3,.flac,.ogg"
              onChange={(event) => {
                const nuevo = event.target.files?.[0];
                event.target.value = "";
                if (nuevo) void analizar(nuevo);
              }}
            />
            <button
              className="primary"
              onClick={() => entrada.current?.click()}
              disabled={cargando}
            >
              {cargando ? "Analizando…" : "Subir audio"}
            </button>
          </div>
        </div>
        {!archivo && (
          <p className="empty-small">
            Sube una canción (wav, mp3, flac u ogg). Se parte en tramos de 10 s cada 5 s y cada
            tramo busca los 5 tramos más parecidos entre los 999 clips de GTZAN con el índice
            HNSW.
          </p>
        )}
        {archivo && (
          <div className="copyright-audio">
            <strong title={archivo.name}>{archivo.name}</strong>
            <audio
              ref={reproductor}
              controls
              src={url}
              onTimeUpdate={(event) => setActual(event.currentTarget.currentTime)}
            />
          </div>
        )}
        {cargando && (
          <p className="empty-small" role="status">
            Extrayendo los MFCC de cada tramo y buscando coincidencias…
          </p>
        )}
        {error && (
          <p className="message error copyright-estado" role="alert">
            {error}
          </p>
        )}
      </section>

      {analisis && (
        <>
          <section
            className={`panel copyright-veredicto ${sospechoso ? "sospechoso" : "limpio"}`}
            aria-label="Veredicto"
          >
            <strong>{sospechoso ? "Posible uso no autorizado" : "Sin coincidencias"}</strong>
            <span>
              Umbral: similitud de {analisis.umbral} % o más en al menos{" "}
              {analisis.minimo_tramos} tramos de la misma canción ·{" "}
              {analisis.tramos.length} tramos de {analisis.duracion_tramo} s en{" "}
              {segundos(analisis.duracion)} · {analisis.tiempo_ms.toLocaleString("es-PE")} ms
            </span>
          </section>

          <section className="panel" aria-labelledby="timeline-titulo">
            <div className="panel-heading">
              <h2 id="timeline-titulo">Línea de tiempo</h2>
              <span className="muted small">
                color = canción original · intensidad = similitud
              </span>
            </div>
            <Timeline
              analisis={analisis}
              colores={colores}
              actual={actual}
              reproducirDesde={reproducirDesde}
            />
          </section>

          <section className="panel" aria-labelledby="detectadas-titulo">
            <div className="panel-heading">
              <h2 id="detectadas-titulo">Canciones detectadas</h2>
            </div>
            {!analisis.canciones.length && (
              <p className="empty-small">Ningún tramo supera el umbral.</p>
            )}
            <ul className="copyright-canciones" aria-label="Canciones detectadas">
              {analisis.canciones.map((cancion) => (
                <li key={cancion.cancion_id}>
                  <i
                    className="copyright-color"
                    style={{ background: colores.get(cancion.cancion_id) }}
                  />
                  <div className="copyright-cancion">
                    <strong>{nombreCancion(cancion.archivo)}</strong>
                    <span className="muted small">
                      {cancion.genero} · {cancion.tramos}{" "}
                      {cancion.tramos === 1 ? "tramo" : "tramos"}
                      {cancion.coincide ? "" : " · no alcanza el mínimo de tramos"}
                    </span>
                  </div>
                  <span className="copyright-porcentaje">{cancion.porcentaje} %</span>
                  <button
                    aria-pressed={escuchando === cancion.cancion_id}
                    onClick={() =>
                      setEscuchando(
                        escuchando === cancion.cancion_id ? undefined : cancion.cancion_id,
                      )
                    }
                  >
                    {escuchando === cancion.cancion_id ? "Ocultar" : "Escuchar original"}
                  </button>
                  {escuchando === cancion.cancion_id && (
                    <audio
                      className="copyright-original"
                      controls
                      autoPlay
                      src={`/api/audios/${cancion.cancion_id}`}
                      aria-label={`Original ${nombreCancion(cancion.archivo)}`}
                    />
                  )}
                </li>
              ))}
            </ul>
            <details className="copyright-sql">
              <summary>Consultas SQL</summary>
              {analisis.consultas.map((consulta) => (
                <code key={consulta}>{consulta}</code>
              ))}
            </details>
          </section>
        </>
      )}
    </div>
  );
}
