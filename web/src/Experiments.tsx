import { useEffect, useState } from "react";
import { request } from "./api";
import type { Experiment } from "./types";

type Fila = Record<string, string>;

// paleta categorica en orden fijo: el color sigue a la estructura, no a su posicion
const COLORES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"];
const SIN_COLOR = "#8a8985";
const IDENTIDAD = ["estructura", "implementacion", "indice", "tecnica"];
// los archivos de ranking (score por posicion) no son mediciones: se ven en la tabla
const MEDIDA = /(^t_|_ms$|_us$|bytes|^paginas|_pag|recall|^p10$|^r10|^filas$|^resultados$|^postings$|vectores_comparados|^altura$|^aciertos$|^tumbas$|^registros_|^reorganizaciones)/;
const IGNORAR = new Set(["semilla", "id", "texto", "posicion", "soporta_rango"]);
const MAX_VALORES_FILTRO = 40;
const TODOS = "*";
const NOMBRES: Record<string, string> = {
  operacion: "Operación",
  parametro: "Parámetro",
  barajado: "Orden de inserción",
  metrica: "Métrica de distancia",
  palabras: "Palabras",
  dimension: "Dimensión",
  origen: "Origen",
  experimento: "Experimento",
  parametros: "Parámetros",
  consulta: "Consulta",
  M: "M",
  ef_construccion: "efConstruction",
  ef_busqueda: "efSearch",
  fraccion_borrada: "Fracción borrada",
  reconstrucciones: "Reconstrucciones",
  nlist: "nlist",
  nprobe: "nprobe",
  soporta_rango: "Soporta rango",
};
const nombreColumna = (columna: string) =>
  NOMBRES[columna] ?? columna.charAt(0).toUpperCase() + columna.slice(1).replace(/_/g, " ");

const numero = (valor: string | undefined) => (valor?.trim() ? Number(valor) : Number.NaN);
const esNumero = (valor: string | undefined) => Number.isFinite(numero(valor));

function cifra(valor: number, decimales = 0) {
  const [entero, fraccion] = Math.abs(valor).toFixed(decimales).split(".");
  const agrupado = entero.replace(/\B(?=(\d{3})+(?!\d))/g, " ");
  return `${valor < 0 ? "−" : ""}${agrupado}${fraccion ? `,${fraccion}` : ""}`;
}

function decimalesPara(valor: number) {
  return valor >= 100 ? 0 : valor >= 10 ? 1 : 2;
}

function formatear(columna: string, valor: number) {
  if (columna.endsWith("_us")) {
    return valor >= 1000 ? `${cifra(valor / 1000, decimalesPara(valor / 1000))} ms` : `${cifra(valor, decimalesPara(valor))} µs`;
  }
  if (columna.endsWith("_ms")) {
    return valor >= 1000 ? `${cifra(valor / 1000, decimalesPara(valor / 1000))} s` : `${cifra(valor, decimalesPara(valor))} ms`;
  }
  if (columna.includes("bytes")) {
    if (valor >= 1024 ** 2) return `${cifra(valor / 1024 ** 2, 1)} MB`;
    if (valor >= 1024) return `${cifra(valor / 1024, 1)} KB`;
    return `${cifra(valor)} B`;
  }
  return cifra(valor, Number.isInteger(valor) ? 0 : decimalesPara(valor));
}

function nombreMetrica(columna: string) {
  const unidad = columna.endsWith("_us") ? " (µs)" : columna.endsWith("_ms") ? " (ms)" : columna.includes("bytes") ? " (bytes)" : "";
  if (columna.includes("bytes")) {
    const resto = columna.replace(/_?bytes_?/, " ").replace(/_/g, " ").trim();
    return `Espacio${resto ? ` ${resto}` : ""}${unidad}`;
  }
  const texto = columna
    .replace(/^t_/, "")
    .replace(/_(us|ms)$/, "")
    .replace(/\bprom\b/, "promedio")
    .replace(/\bpk\b/, "PK")
    .replace(/(^|_)pag(_|$)/g, "$1páginas$2")
    .replace(/_/g, " ")
    .replace(/paginas/g, "páginas")
    .replace(/cion\b/g, "ción")
    .replace(/busqueda/g, "búsqueda");
  return texto.charAt(0).toUpperCase() + texto.slice(1) + unidad;
}

// en la etiqueta de una barra, un numero suelto se acompana del nombre de su columna
function valorDimension(columna: string, valor: string, conNombre = true) {
  if (columna === "barajado") return valor === "1" ? "barajado" : "ordenado";
  if (!esNumero(valor)) return valor;
  const texto = cifra(Number(valor), Number.isInteger(Number(valor)) ? 0 : 2);
  return conNombre ? `${NOMBRES[columna] ?? columna.replace(/_/g, " ")} ${texto}` : texto;
}

export default function Experiments() {
  const [files, setFiles] = useState<Experiment[]>([]);
  const [selected, setSelected] = useState("");
  const [metric, setMetric] = useState("t_busqueda_pk_prom_us");
  const [filtros, setFiltros] = useState<Record<string, string>>({});
  const [logaritmica, setLogaritmica] = useState(false);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);
  async function refresh() {
    setLoading(true);
    setError("");
    try {
      const data = await request<{ archivos: Experiment[] }>("experimentos");
      setFiles(data.archivos);
    } catch (error) {
      setError((error as Error).message);
    } finally {
      setLoading(false);
    }
  }
  useEffect(() => {
    void refresh();
  }, []);

  const file = files.find((item) => item.nombre === selected) ?? files[0];
  const columnas = file?.columnas ?? [];
  const filas = file?.filas ?? [];
  const identidad = IDENTIDAD.find((columna) => columnas.includes(columna)) ?? "";
  const metrics = columnas.filter(
    (columna) =>
      columna !== identidad &&
      MEDIDA.test(columna) &&
      filas.some((fila) => esNumero(fila[columna])) &&
      filas.every((fila) => !fila[columna]?.trim() || esNumero(fila[columna])),
  );
  const activeMetric = metrics.includes(metric) ? metric : (metrics[0] ?? "");
  const dimensiones = columnas.filter((columna) => {
    if (columna === identidad || columna === "n" || metrics.includes(columna) || IGNORAR.has(columna)) return false;
    const distintos = new Set(filas.map((fila) => fila[columna]).filter((valor) => valor?.trim()));
    return distintos.size >= 2 && distintos.size <= MAX_VALORES_FILTRO;
  });
  // columnas descriptivas que no sirven de filtro: si cambian van en la etiqueta de cada
  // barra; si valen lo mismo en todo el archivo se dicen una sola vez arriba
  const descriptivas = columnas.filter(
    (columna) =>
      columna !== identidad && columna !== "n" && !metrics.includes(columna) &&
      !dimensiones.includes(columna) && !IGNORAR.has(columna) && !MEDIDA.test(columna),
  );
  const distintos = (columna: string) => new Set(filas.map((fila) => fila[columna]).filter((valor) => valor?.trim())).size;
  const extras = descriptivas.filter((columna) => distintos(columna) > 1);
  const constantes = descriptivas.filter((columna) => distintos(columna) === 1);
  const contexto = constantes
    .map((columna) => valorDimension(columna, filas.find((fila) => fila[columna]?.trim())?.[columna] ?? ""))
    .join(" · ");
  const entidades = [...new Set(filas.map((fila) => fila[identidad] ?? ""))].filter(Boolean);
  const color = (entidad: string) => {
    const posicion = entidades.indexOf(entidad);
    return posicion >= 0 && posicion < COLORES.length ? COLORES[posicion] : SIN_COLOR;
  };

  // cada filtro solo ofrece los valores que existen con los anteriores; si no se eligio
  // nada, toma el primero, para que la grafica compare estructuras y no liste todo
  let base = filas.filter((fila) => esNumero(fila[activeMetric]) && numero(fila[activeMetric]) >= 0);
  const opciones: Record<string, string[]> = {};
  const efectivos: Record<string, string> = {};
  for (const columna of dimensiones) {
    const valoresColumna = [...new Set(base.map((fila) => fila[columna]).filter((valor) => valor?.trim()))];
    const elegido = filtros[columna];
    const valor = elegido === TODOS ? "" : elegido && valoresColumna.includes(elegido) ? elegido : (valoresColumna[0] ?? "");
    opciones[columna] = valoresColumna;
    efectivos[columna] = valor;
    // una fila sin valor en esta columna (fuerza bruta no tiene M) no se descarta
    if (valor) base = base.filter((fila) => !fila[columna]?.trim() || fila[columna] === valor);
  }
  // las corridas repetidas de la misma medicion se promedian en una sola barra
  const promedios = new Map<string, { fila: Fila; suma: number; corridas: number }>();
  for (const fila of base) {
    const clave = [identidad, "n", ...dimensiones, ...extras].map((columna) => fila[columna] ?? "").join("\u0000");
    const actual = promedios.get(clave);
    if (actual) {
      actual.suma += numero(fila[activeMetric]);
      actual.corridas += 1;
    } else {
      promedios.set(clave, { fila, suma: numero(fila[activeMetric]), corridas: 1 });
    }
  }
  const visibles = [...promedios.values()]
    .map(({ fila, suma, corridas }) => ({ fila, valor: suma / corridas, corridas }))
    .sort((a, b) => (numero(a.fila.n) || 0) - (numero(b.fila.n) || 0));
  const valores = visibles.map((medicion) => medicion.valor);
  const maximo = Math.max(...valores, 0);
  const minimoPositivo = Math.min(...valores.filter((valor) => valor > 0), maximo || 1);
  const usarLog = logaritmica && maximo > 0 && maximo / minimoPositivo > 1;
  const ancho = (valor: number) => {
    if (maximo <= 0 || valor <= 0) return 0;
    if (!usarLog) return (valor / maximo) * 100;
    const base = Math.log10(minimoPositivo) - 0.3;
    return ((Math.log10(valor) - base) / (Math.log10(maximo) - base)) * 100;
  };
  const grupos: { n: string; mediciones: typeof visibles }[] = [];
  for (const medicion of visibles) {
    const n = medicion.fila.n ?? "";
    if (grupos.at(-1)?.n !== n) grupos.push({ n, mediciones: [] });
    grupos.at(-1)!.mediciones.push(medicion);
  }
  const elegirArchivo = (nombre: string) => {
    setSelected(nombre);
    setMetric("");
    setFiltros({});
  };

  return (
    <div className="experiments">
      <section className="panel">
        <div className="panel-heading">
          <h2>Benchmarks</h2>
          <button onClick={refresh} disabled={loading}>
            {loading ? "Leyendo…" : "Actualizar"}
          </button>
        </div>
        <div className="section-body">
          <p>
            CSV de <code>datos/resultados/</code> generados con{" "}
            <code>make bench</code>.
          </p>
          {error && (
            <p className="message error" role="alert">
              {error}
            </p>
          )}
          {!files.length && !loading && (
            <p className="empty-small">No hay archivos en datos/resultados/.</p>
          )}
          {file && (
            <>
              <div className="filters">
                <label>
                  Archivo
                  <select
                    aria-label="Archivo"
                    value={file.nombre}
                    onChange={(event) => elegirArchivo(event.target.value)}
                  >
                    {files.map((item) => (
                      <option key={item.nombre}>{item.nombre}</option>
                    ))}
                  </select>
                </label>
                {metrics.length > 0 && (
                  <label>
                    Métrica
                    <select
                      aria-label="Métrica"
                      value={activeMetric}
                      onChange={(event) => setMetric(event.target.value)}
                    >
                      {metrics.map((item) => (
                        <option key={item} value={item}>
                          {nombreMetrica(item)}
                        </option>
                      ))}
                    </select>
                  </label>
                )}
                {dimensiones.map((columna) => (
                  <label key={columna} className="filtro-corto">
                    {nombreColumna(columna)}
                    <select
                      value={efectivos[columna] || TODOS}
                      onChange={(event) => setFiltros({ ...filtros, [columna]: event.target.value })}
                    >
                      <option value={TODOS}>Todos</option>
                      {opciones[columna].map((valor) => (
                        <option key={valor} value={valor}>
                          {valorDimension(columna, valor, false)}
                        </option>
                      ))}
                    </select>
                  </label>
                ))}
                {metrics.length > 0 && (
                  <label className="filtro-check">
                    <input
                      type="checkbox"
                      checked={logaritmica}
                      onChange={(event) => setLogaritmica(event.target.checked)}
                    />
                    Escala logarítmica
                  </label>
                )}
              </div>

              {contexto && <p className="hint">En todo el archivo: {contexto}</p>}
              {entidades.length >= 2 && (
                <ul className="chart-legend" aria-label="Leyenda">
                  {entidades.map((entidad) => (
                    <li key={entidad}>
                      <span className="chart-swatch" style={{ background: color(entidad) }} />
                      {entidad}
                    </li>
                  ))}
                </ul>
              )}

              {!metrics.length ? (
                <p className="empty-small">Este archivo no tiene columnas de medición para graficar; los datos están abajo.</p>
              ) : !visibles.length ? (
                <p className="empty-small">Ninguna medición coincide con los filtros.</p>
              ) : (
                <div
                  className="bar-chart"
                  role="img"
                  aria-label={`Gráfica de ${nombreMetrica(activeMetric)}`}
                >
                  {grupos.map((grupo) => (
                    <div className="chart-group" key={grupo.n}>
                      {grupo.n && (
                        <h3 className="chart-group-title">
                          n = {cifra(numero(grupo.n))}
                        </h3>
                      )}
                      {grupo.mediciones.map(({ fila, valor, corridas }, index) => {
                        const entidad = fila[identidad] ?? "";
                        const detalle = [
                          ...[...dimensiones.filter((columna) => !efectivos[columna]), ...extras]
                            .filter((columna) => fila[columna]?.trim())
                            .map((columna) => valorDimension(columna, fila[columna])),
                          ...(corridas > 1 ? [`promedio de ${corridas} corridas`] : []),
                        ].join(" · ");
                        return (
                          <div className="chart-row" key={index} tabIndex={0}>
                            <span className="chart-label">
                              <span className="chart-swatch" style={{ background: color(entidad) }} />
                              <span>
                                {entidad || "Medición"}
                                {detalle && <small>{detalle}</small>}
                              </span>
                            </span>
                            <div className="bar-track">
                              <div style={{ width: `${ancho(valor)}%`, background: color(entidad) }} />
                            </div>
                            <strong>{formatear(activeMetric, valor)}</strong>
                            <div className="chart-tip" role="tooltip">
                              {corridas > 1 && (
                                <span>
                                  <em>{nombreMetrica(activeMetric)}</em> {formatear(activeMetric, valor)} (promedio de {corridas}; abajo, la primera corrida)
                                </span>
                              )}
                              {columnas
                                .filter((columna) => fila[columna]?.trim())
                                .map((columna) => (
                                  <span key={columna}>
                                    <em>{columna}</em> {fila[columna]}
                                  </span>
                                ))}
                            </div>
                          </div>
                        );
                      })}
                    </div>
                  ))}
                </div>
              )}
              {usarLog && (
                <p className="hint">Escala logarítmica: el largo de cada barra es proporcional al logaritmo del valor.</p>
              )}
              <details>
                <summary>
                  Ver datos del archivo ({file.filas.length} mediciones)
                </summary>
                <div className="table-scroll">
                  <table>
                    <thead>
                      <tr>
                        {file.columnas.map((column) => (
                          <th key={column}>{column}</th>
                        ))}
                      </tr>
                    </thead>
                    <tbody>
                      {file.filas.map((row, i) => (
                        <tr key={i}>
                          {file.columnas.map((column) => (
                            <td key={column}>{row[column]}</td>
                          ))}
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </details>
            </>
          )}
        </div>
      </section>
    </div>
  );
}
