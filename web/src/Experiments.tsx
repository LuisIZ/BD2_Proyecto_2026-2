import { useEffect, useState } from "react";
import { request } from "./api";
import type { Experiment } from "./types";

export default function Experiments() {
  const [files, setFiles] = useState<Experiment[]>([]);
  const [selected, setSelected] = useState("");
  const [metric, setMetric] = useState("t_busqueda_pk_prom_us");
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
  const metrics =
    file?.columnas.filter(
      (name) => name.startsWith("t_") || name.includes("bytes"),
    ) ?? [];
  const activeMetric = metrics.includes(metric) ? metric : (metrics[0] ?? "");
  const validRows =
    file?.filas.filter(
      (row) =>
        row[activeMetric]?.trim() &&
        Number.isFinite(Number(row[activeMetric])) &&
        Number(row[activeMetric]) >= 0,
    ) ?? [];
  const maximum = Math.max(
    1,
    ...validRows.map((row) => Number(row[activeMetric])),
  );

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
                    onChange={(event) => setSelected(event.target.value)}
                  >
                    {files.map((item) => (
                      <option key={item.nombre}>{item.nombre}</option>
                    ))}
                  </select>
                </label>
                <label>
                  Métrica
                  <select
                    aria-label="Métrica"
                    value={activeMetric}
                    onChange={(event) => setMetric(event.target.value)}
                  >
                    {metrics.map((item) => (
                      <option key={item}>{item}</option>
                    ))}
                  </select>
                </label>
              </div>
              <p className="hint">
                <code>_us</code> en microsegundos · espacio en bytes
              </p>
              <div
                className="bar-chart"
                role="img"
                aria-label={`Gráfica de ${activeMetric}`}
              >
                {validRows.map((row, index) => (
                  <div className="chart-row" key={index}>
                    <span>
                      {row.implementacion ?? "Medición"} ·{" "}
                      {Number(row.n || 0).toLocaleString("es-PE")} reg.
                      <small>
                        {row.modo ?? ""}
                        {row.barajado !== undefined
                          ? row.barajado === "1"
                            ? " · barajado"
                            : " · ordenado"
                          : ""}
                      </small>
                    </span>
                    <div className="bar-track">
                      <div
                        style={{
                          width: `${(Number(row[activeMetric]) / maximum) * 100}%`,
                        }}
                      />
                    </div>
                    <strong>
                      {Number(row[activeMetric]).toLocaleString("es-PE")}
                    </strong>
                  </div>
                ))}
              </div>
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
