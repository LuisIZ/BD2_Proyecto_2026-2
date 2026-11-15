import { useEffect, useMemo, useRef, useState } from "react";
import Timeline, { COLORES, nombreCancion, segundos } from "./Timeline";
import { analizarAudio } from "./api";
import { TRAMOS_MINIMOS, type Analisis } from "./types";

/** El VECTOR(...) de 256 números de una consulta, recortado a los 3 primeros. */
const recortarVector = (sql: string) =>
  sql.replace(/VECTOR\(([^,)]*,[^,)]*,[^,)]*),[^)]*\)/g, "VECTOR($1, ...)");

/** Vista "Copyright" (#94): sube un audio y muestra qué tramos coinciden con canciones originales. */
export default function Copyright() {
  const entrada = useRef<HTMLInputElement>(null);
  const reproductor = useRef<HTMLAudioElement>(null);
  const [archivo, setArchivo] = useState<File>();
  const [url, setUrl] = useState("");
  const [analisis, setAnalisis] = useState<Analisis>();
  const [tiempo, setTiempo] = useState(0);
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
    const inicio = performance.now();
    try {
      setAnalisis(await analizarAudio(nuevo));
      setTiempo(performance.now() - inicio);
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
  const sospechoso = analisis?.veredicto === "posible_copyright";
  // una consulta SIMILAR_TO por tramo y la última trae los nombres de las canciones
  const porTramo = analisis?.consultas.filter((c) => /SIMILAR_TO/i.test(c)) ?? [];
  const otras = analisis?.consultas.filter((c) => !/SIMILAR_TO/i.test(c)) ?? [];

  return (
    <div className="copyright">
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
              src={url || undefined}
              onTimeUpdate={(event) => setActual(event.currentTarget.currentTime)}
            />
          </div>
        )}
        {cargando && (
          <p className="empty-small" role="status">
            Extrayendo los MFCC de cada tramo y buscando coincidencias… (unos segundos por
            minuto de audio)
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
              Umbral: similitud de {analisis.umbral} % o más en al menos {TRAMOS_MINIMOS} tramos
              de la misma canción · {analisis.tramos.length} tramos de 10 s en{" "}
              {segundos(analisis.duracion)} ·{" "}
              {(tiempo / 1000).toLocaleString("es-PE", { maximumFractionDigits: 1 })} s
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
                      {cancion.tramos === 1 ? "tramo" : "tramos"} · similitud máxima{" "}
                      {cancion.similitud_maxima} %
                    </span>
                  </div>
                  <span
                    className="copyright-porcentaje"
                    title="parte de los tramos de la canción subida donde aparece"
                  >
                    {cancion.porcentaje} %<small> de los tramos</small>
                  </span>
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
              {porTramo.length > 0 && (
                <>
                  <p className="small">
                    Una por cada tramo con sonido ({porTramo.length}), en lotes:
                  </p>
                  <code>{recortarVector(porTramo[0])}</code>
                </>
              )}
              {otras.map((consulta) => (
                <code key={consulta}>{consulta}</code>
              ))}
            </details>
          </section>
        </>
      )}
    </div>
  );
}
