import { useEffect, useState } from "react";
import { request } from "./api";
import CompareChart from "./CompareChart";
import {
  benchmarkColor,
  benchmarkPoints,
  metricLabel,
  metricUnit,
} from "./benchmark";
import type { Experiment } from "./types";

export default function Experiments() {
  const [files, setFiles] = useState<Experiment[]>([]);
  const [selected, setSelected] = useState("");
  const [operation, setOperation] = useState("");
  const [size, setSize] = useState("");
  const [metric, setMetric] = useState("");
  const [selectedStructures, setSelectedStructures] = useState<string[]>();
  const [logarithmic, setLogarithmic] = useState(false);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(true);

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
  const points = file ? benchmarkPoints(file) : [];
  const structures = [...new Set(points.map((point) => point.structure))].sort();
  const operations = [...new Set(points.map((point) => point.operation))].sort();
  const activeOperation = operations.includes(operation)
    ? operation
    : (operations[0] ?? "");
  const sizes = [
    ...new Set(
      points
        .filter((point) => point.operation === activeOperation)
        .map((point) => point.size),
    ),
  ].sort((a, b) => a - b);
  const activeSize = sizes.includes(Number(size)) ? Number(size) : sizes[0];
  const metrics = [
    ...new Set(
      points
        .filter(
          (point) =>
            point.operation === activeOperation && point.size === activeSize,
        )
        .map((point) => point.metric),
    ),
  ].sort();
  const activeMetric = metrics.includes(metric) ? metric : (metrics[0] ?? "");
  const visibleStructures =
    selectedStructures === undefined
      ? structures
      : selectedStructures.filter((name) => structures.includes(name));
  const bars = visibleStructures.flatMap((structure) => {
    const samples = points
      .filter(
        (point) =>
          point.structure === structure &&
          point.operation === activeOperation &&
          point.size === activeSize &&
          point.metric === activeMetric,
      )
      .map((point) => point.value);
    if (!samples.length) return [];
    return [
      {
        id: structure,
        label: structure,
        color: benchmarkColor(structure),
        value: samples.reduce((total, sample) => total + sample, 0) / samples.length,
        note: samples.length > 1 ? `${samples.length} mediciones · promedio` : undefined,
      },
    ];
  });
  const unit = metricUnit(activeMetric);
  const formatValue = (value: number) =>
    value.toLocaleString("es-PE", {
      maximumFractionDigits: unit === "bytes" || unit === "páginas" ? 0 : 3,
    });
  const chartTitle =
    `Gráfica de ${activeOperation} · ${metricLabel(activeMetric)}`;

  function selectFile(name: string) {
    setSelected(name);
    setOperation("");
    setSize("");
    setMetric("");
    setSelectedStructures(undefined);
  }

  function toggleStructure(name: string, checked: boolean) {
    setSelectedStructures((previous) => {
      const current = previous ?? structures;
      return checked
        ? [...current, name]
        : current.filter((item) => item !== name);
    });
  }

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
            Resultados CSV y JSON de los harnesses en{" "}
            <code>datos/resultados/</code>.
          </p>
          {loading && (
            <p className="hint" role="status">
              Cargando mediciones…
            </p>
          )}
          {error && (
            <p className="message error" role="alert">
              {error}
            </p>
          )}
          {!files.length && !loading && !error && (
            <p className="empty-small">
              No hay archivos CSV o JSON en datos/resultados/.
            </p>
          )}
          {file && (
            <>
              <div className="filters">
                <label>
                  Archivo
                  <select
                    aria-label="Archivo"
                    value={file.nombre}
                    onChange={(event) => selectFile(event.target.value)}
                  >
                    {files.map((item) => (
                      <option key={item.nombre} value={item.nombre}>
                        {item.nombre}
                      </option>
                    ))}
                  </select>
                </label>
                {points.length > 0 && (
                  <>
                    <label>
                      Operación
                      <select
                        aria-label="Operación"
                        value={activeOperation}
                        onChange={(event) => {
                          setOperation(event.target.value);
                          setSize("");
                          setMetric("");
                        }}
                      >
                        {operations.map((item) => (
                          <option key={item}>{item}</option>
                        ))}
                      </select>
                    </label>
                    <label>
                      Tamaño del dataset
                      <select
                        aria-label="Tamaño del dataset"
                        value={activeSize}
                        onChange={(event) => {
                          setSize(event.target.value);
                          setMetric("");
                        }}
                      >
                        {sizes.map((item) => (
                          <option key={item} value={item}>
                            {item.toLocaleString("es-PE")} registros
                          </option>
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
                          <option key={item} value={item}>
                            {metricLabel(item)}
                          </option>
                        ))}
                      </select>
                    </label>
                    <label className="scale-toggle">
                      <input
                        type="checkbox"
                        checked={logarithmic}
                        onChange={(event) =>
                          setLogarithmic(event.target.checked)
                        }
                      />
                      Escala logarítmica
                    </label>
                  </>
                )}
              </div>
              {points.length > 0 ? (
                <>
                  <fieldset className="structure-filter">
                    <legend>Estructuras</legend>
                    <div className="structure-options">
                      {structures.map((name) => (
                        <label key={name}>
                          <input
                            type="checkbox"
                            aria-label={`Estructura ${name}`}
                            checked={visibleStructures.includes(name)}
                            onChange={(event) =>
                              toggleStructure(name, event.target.checked)
                            }
                          />
                          <span
                            className="swatch"
                            style={{ background: benchmarkColor(name) }}
                          />
                          {name}
                        </label>
                      ))}
                    </div>
                  </fieldset>
                  {bars.length ? (
                    <>
                      <p className="hint">
                        {unit} · {activeOperation} ·{" "}
                        {activeSize.toLocaleString("es-PE")} registros
                      </p>
                      <div className="benchmark-chart">
                        <CompareChart
                          title={chartTitle}
                          unit={unit}
                          format={formatValue}
                          bars={bars}
                          logarithmic={logarithmic}
                        />
                      </div>
                    </>
                  ) : (
                    <p className="empty-small" role="status">
                      Selecciona al menos una estructura para mostrar el
                      gráfico.
                    </p>
                  )}
                </>
              ) : (
                !loading && (
                  <p className="empty-small" role="status">
                    Este archivo no contiene mediciones con campos reconocibles
                    de estructura, operación y tamaño del dataset.
                  </p>
                )
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
