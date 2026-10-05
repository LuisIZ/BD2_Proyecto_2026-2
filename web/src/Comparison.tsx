import { useEffect, useState } from "react";
import { request } from "./api";
import type { Result, Table } from "./types";
import { Plan, Results, labels } from "./Results";
import RangeChart from "./RangeChart";
import CompareChart from "./CompareChart";
import {
  accessStep,
  cases,
  downloadCsv,
  elapsed,
  formatBytes,
  formatNumber,
  median,
  pagesRead,
  pagesWritten,
  setupStatements,
  signature,
  structures,
  sweepSql,
  sweepWidths,
} from "./structures";
import type { StructureId } from "./structures";
import { datasetLabel, datasetRows } from "./sql";

interface CaseRun {
  caseId: string;
  reps: number;
  statements: Record<StructureId, string[]>;
  measures: { id: StructureId; runs: Result[][] }[];
}

interface SweepRow {
  rows: number;
  data: Record<StructureId, { pages: number; ms: number; count: number }>;
}

async function execute(statements: string[]) {
  const data = await request<{ resultados: Result[] }>("consultas", {
    sql: statements.join("\n"),
  });
  if (data.resultados.length !== statements.length)
    throw new Error("El motor devolvió una cantidad distinta de resultados.");
  return data.resultados;
}

export default function Comparison({
  tables,
  connected,
  refresh,
  openSql,
}: {
  tables: Table[];
  connected: boolean;
  refresh: () => Promise<void>;
  openSql: (sql: string) => void;
}) {
  const [datasets, setDatasets] = useState<string[]>([]);
  const [csv, setCsv] = useState("organizations-10000.csv");
  const [loadTimes, setLoadTimes] = useState<
    Partial<Record<StructureId, number>>
  >({});
  const [task, setTask] = useState("");
  const [error, setError] = useState("");
  const [caseId, setCaseId] = useState("clave");
  const [edited, setEdited] = useState<Record<string, number>>({});
  const [reps, setReps] = useState(1);
  const [run, setRun] = useState<CaseRun>();
  const [selected, setSelected] = useState<StructureId>("bplus");
  const [statement, setStatement] = useState(0);
  const [sweep, setSweep] = useState<SweepRow[]>();
  const [metric, setMetric] = useState<"pages" | "ms">("pages");
  const [chartStructures, setChartStructures] = useState<StructureId[]>(
    structures.map((structure) => structure.id),
  );
  const [chartScale, setChartScale] = useState<"auto" | "linear" | "log">(
    "auto",
  );

  useEffect(() => {
    if (!connected) return;
    request<{ archivos: string[] }>("datasets")
      .then((data) =>
        setDatasets(
          [...data.archivos].sort((a, b) => datasetRows(a) - datasetRows(b)),
        ),
      )
      .catch((error) => setError(error.message));
  }, [connected]);

  const found = structures.map((s) =>
    tables.find((table) => table.nombre.toLowerCase() === s.table),
  );
  const counts = found.map((table) =>
    Number(table?.estadisticas.registros ?? 0),
  );
  const n = counts[0];
  const ready =
    found.every(Boolean) &&
    !!found[3]?.indices.some((index) => index.columna === "Index");
  const sameSize = counts.every((count) => count === n);
  const busy = task !== "";
  const testCase = cases.find((item) => item.id === caseId)!;
  const params = Object.fromEntries(
    testCase.params.map((p) => [
      p.name,
      edited[`${caseId}.${p.name}`] ?? p.initial(n || 1000),
    ]),
  );

  async function work(label: string, job: () => Promise<void>) {
    if (busy) return;
    setTask(label);
    setError("");
    try {
      await job();
    } catch (error) {
      setError((error as Error).message);
    } finally {
      await refresh();
      setTask("");
    }
  }

  const prepare = () =>
    work(`Creando las cuatro tablas con ${csv}…`, async () => {
      const existing = tables.map((table) => table.nombre.toLowerCase());
      const { drops, statements } = setupStatements(csv, existing);
      const results = await execute(statements);
      const time = (i: number) => results[drops.length + i]?.tiempo_ms ?? 0;
      setLoadTimes({
        heap: time(0),
        seq: time(1),
        bplus: time(2),
        idx: time(3) + time(4) + time(5),
      });
      setRun(undefined);
      setSweep(undefined);
      setEdited({});
      const failed = results.find((result) => !result.ok);
      if (failed) throw new Error(failed.error);
    });

  const runCase = () =>
    work(`${testCase.label} en las cuatro tablas…`, async () => {
      const statements = Object.fromEntries(
        structures.map((s) => [s.id, testCase.sql(s.table, params)]),
      ) as Record<StructureId, string[]>;
      const size = statements.heap.length;
      const batch: string[] = [];
      for (let r = 0; r < reps; r++)
        for (const s of structures) batch.push(...statements[s.id]);
      const results = await execute(batch);
      const measures = structures.map((s, si) => ({
        id: s.id,
        runs: Array.from({ length: reps }, (_, r) => {
          const start = (r * structures.length + si) * size;
          return results.slice(start, start + size);
        }),
      }));
      setRun({ caseId, reps, statements, measures });
      setStatement(0);
      const failed = results.find((result) => !result.ok);
      if (failed) throw new Error(failed.error);
    });

  const runSweep = () =>
    work("Midiendo rangos crecientes…", async () => {
      const widths = sweepWidths.filter((width) => width <= n);
      const batch: string[] = [];
      for (let r = 0; r < reps; r++)
        for (const width of widths)
          for (const s of structures) batch.push(sweepSql(s.table, width));
      const results = await execute(batch);
      const failed = results.find((result) => !result.ok);
      if (failed) throw new Error(failed.error);
      setSweep(
        widths.map((width, wi) => ({
          rows: width,
          data: Object.fromEntries(
            structures.map((s, si) => {
              const samples = Array.from(
                { length: reps },
                (_, r) =>
                  results[(r * widths.length + wi) * structures.length + si],
              );
              const last = samples[samples.length - 1];
              return [
                s.id,
                {
                  pages: pagesRead([last]),
                  ms: median(samples.map((sample) => sample.tiempo_ms ?? 0)),
                  count: Number(last.filas?.[0]?.[0] ?? -1),
                },
              ];
            }),
          ) as SweepRow["data"],
        })),
      );
    });

  const runInfo = run && cases.find((item) => item.id === run.caseId)!;
  const rows = run?.measures.map((m) => {
    const last = m.runs[m.runs.length - 1];
    return {
      id: m.id,
      last,
      step: accessStep(last),
      read: pagesRead(last),
      written: pagesWritten(last),
      ms: median(m.runs.map(elapsed)),
    };
  });
  const maxRead = Math.max(1, ...(rows ?? []).map((row) => row.read));
  const signatures = run?.measures.flatMap((m) =>
    m.runs.map((r) => signature(r, runInfo!.compare)),
  );
  const allOk = !!run?.measures.every((m) =>
    m.runs.every((r) => r.every((x) => x.ok)),
  );
  const same =
    allOk &&
    !!signatures?.every((s) => s === signatures[0]) &&
    (runInfo?.compare !== "changes" || signatures?.[0] === "[1,1,1,0]");
  const detail = rows?.find((row) => row.id === selected);
  const chartRows = rows?.filter((row) => chartStructures.includes(row.id));
  const returned = rows?.[0]?.last[0]?.filas?.length ?? 0;
  const chartControls = (
    <div className="chart-controls">
      <fieldset className="structure-filter">
        <legend>Estructuras mostradas</legend>
        <div className="structure-options">
          {structures.map((structure) => (
            <label key={structure.id}>
              <input
                type="checkbox"
                aria-label={`Estructura ${structure.label}`}
                checked={chartStructures.includes(structure.id)}
                onChange={(event) =>
                  setChartStructures((previous) =>
                    event.target.checked
                      ? [...previous, structure.id]
                      : previous.filter((item) => item !== structure.id),
                  )
                }
              />
              <span
                className="swatch"
                style={{ background: structure.color }}
              />
              {structure.label}
            </label>
          ))}
        </div>
      </fieldset>
      <label className="scale-toggle">
        Escala
        <select
          aria-label="Escala del gráfico"
          value={chartScale}
          onChange={(event) => {
            const value = event.target.value;
            if (value === "auto" || value === "linear" || value === "log")
              setChartScale(value);
          }}
        >
          <option value="auto">Automática</option>
          <option value="linear">Lineal</option>
          <option value="log">Logarítmica</option>
        </select>
      </label>
    </div>
  );

  function exportCase() {
    if (!run || !rows) return;
    downloadCsv(`caso-${run.caseId}.csv`, [
      [
        "caso",
        "tabla",
        "estructura",
        "acceso",
        "paginas_leidas",
        "paginas_escritas",
        "tiempo_ms_mediana",
        "repeticiones",
      ],
      ...rows.map((row) => {
        const s = structures.find((item) => item.id === row.id)!;
        return [
          run.caseId,
          s.table,
          s.label,
          row.step?.operacion ?? "",
          row.read,
          row.written,
          row.ms.toFixed(4),
          run.reps,
        ];
      }),
    ]);
  }

  function exportSweep() {
    if (!sweep) return;
    downloadCsv("rangos-bplus.csv", [
      [
        "filas",
        "tabla",
        "estructura",
        "paginas_leidas",
        "tiempo_ms_mediana",
        "conteo",
        "repeticiones",
      ],
      ...sweep.flatMap((point) =>
        structures.map((s) => [
          point.rows,
          s.table,
          s.label,
          point.data[s.id].pages,
          point.data[s.id].ms.toFixed(4),
          point.data[s.id].count,
          reps,
        ]),
      ),
    ]);
  }

  const scan = Math.max(
    0,
    ...(sweep ?? []).map((point) => point.data.heap.pages),
  );
  const worse = (point: SweepRow) =>
    metric === "pages"
      ? point.data.idx.pages > scan
      : point.data.idx.ms > point.data.heap.ms;
  const cross = sweep?.findIndex(worse) ?? -1;
  const percent = (rows: number) => formatNumber((rows / n) * 100, 1);
  const loses =
    metric === "pages"
      ? `lee más páginas que recorrer todo el heap (${formatNumber(scan)})`
      : "tarda más que recorrer todo el heap";
  const lastPoint = sweep?.[sweep.length - 1];
  const countsMatch = sweep?.every((point) =>
    structures.every((s) => point.data[s.id].count === point.data.heap.count),
  );
  const repsSelect = (
    <label className="inline-field">
      repeticiones
      <select
        value={reps}
        disabled={busy}
        onChange={(event) => setReps(Number(event.target.value))}
      >
        {[1, 5, 10].map((value) => (
          <option key={value}>{value}</option>
        ))}
      </select>
    </label>
  );

  return (
    <div className="compare">
      {(busy || error) && (
        <p
          className={busy ? "task" : "message error"}
          role={busy ? "status" : "alert"}
        >
          {busy ? task : error}
        </p>
      )}
      {(run || sweep) && chartControls}

      <section className="panel">
        <div className="panel-heading">
          <h2>Tablas de prueba</h2>
          <div className="panel-actions">
            <select
              aria-label="CSV de origen"
              value={csv}
              disabled={busy}
              onChange={(event) => setCsv(event.target.value)}
            >
              {(datasets.length ? datasets : [csv]).map((item) => (
                <option key={item} value={item}>
                  {datasetLabel(item)}
                </option>
              ))}
            </select>
            <button
              className="primary"
              disabled={busy || !connected}
              onClick={prepare}
            >
              {found.some(Boolean) ? "Volver a crear" : "Crear tablas"}
            </button>
          </div>
        </div>
        <p className="panel-note">
          El mismo CSV en cuatro organizaciones. <code>org_idx</code> es un heap
          con índices B+ no agrupados sobre <code>Index</code> y{" "}
          <code>Founded</code>.
        </p>
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Tabla</th>
                <th>Estructura</th>
                <th className="num">Registros</th>
                <th className="num">Páginas</th>
                <th>Espacio en disco</th>
                <th className="num">Carga (ms)</th>
                <th>Estado del archivo</th>
              </tr>
            </thead>
            <tbody>
              {structures.map((s, i) => {
                const table = found[i];
                const indexBytes =
                  table?.indices.reduce(
                    (sum, index) => sum + (index.bytes ?? 0),
                    0,
                  ) ?? 0;
                const bytes = Number(table?.estadisticas.bytes ?? 0);
                const maxBytes = Math.max(
                  1,
                  ...found.map(
                    (t) =>
                      Number(t?.estadisticas.bytes ?? 0) +
                      (t?.indices.reduce(
                        (sum, index) => sum + (index.bytes ?? 0),
                        0,
                      ) ?? 0),
                  ),
                );
                return (
                  <tr key={s.id}>
                    <td>
                      <span
                        className="swatch"
                        style={{ background: s.color }}
                      />
                      <code>{s.table}</code>
                    </td>
                    <td>{s.label}</td>
                    <td className="num">
                      {table ? formatNumber(counts[i]) : "—"}
                    </td>
                    <td className="num">
                      {table
                        ? formatNumber(Number(table.estadisticas.paginas ?? 0))
                        : "—"}
                    </td>
                    <td>
                      {table ? (
                        <span className="bar-cell">
                          <span
                            className="inline-bar"
                            title={`${formatNumber(bytes + indexBytes)} bytes`}
                          >
                            <span
                              style={{
                                width: `${(bytes / maxBytes) * 100}%`,
                                background: s.color,
                              }}
                            />
                            {indexBytes > 0 && (
                              <span
                                className="index-part"
                                style={{
                                  width: `${(indexBytes / maxBytes) * 100}%`,
                                }}
                              />
                            )}
                          </span>
                          {formatBytes(bytes)}
                          {indexBytes > 0 &&
                            ` + ${formatBytes(indexBytes)} índices`}
                        </span>
                      ) : (
                        "—"
                      )}
                    </td>
                    <td className="num">
                      {loadTimes[s.id] !== undefined
                        ? formatNumber(loadTimes[s.id]!, 1)
                        : "—"}
                    </td>
                    <td>
                      <code className="state">
                        {String(table?.estadisticas.detalle ?? "no existe")}
                      </code>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
        {ready && !sameSize && (
          <p className="panel-note warn">
            Las tablas no tienen la misma cantidad de registros. Vuelve a
            crearlas.
          </p>
        )}
      </section>

      <section className="panel">
        <div className="panel-heading">
          <h2>Misma consulta en las cuatro tablas</h2>
          <div className="panel-actions">
            <button
              className="text-button"
              disabled={!run || busy}
              onClick={exportCase}
            >
              Exportar CSV
            </button>
          </div>
        </div>
        <div className="toolbar">
          <div className="segmented" role="group" aria-label="Caso">
            {cases.map((item) => (
              <button
                key={item.id}
                aria-pressed={caseId === item.id}
                disabled={busy}
                onClick={() => setCaseId(item.id)}
              >
                {item.label}
              </button>
            ))}
          </div>
        </div>
        <div className="toolbar">
          {testCase.params.map((p) => (
            <label key={p.name} className="inline-field">
              {p.label}
              <input
                type="number"
                value={params[p.name]}
                disabled={busy}
                onChange={(event) =>
                  setEdited({
                    ...edited,
                    [`${caseId}.${p.name}`]: Number(event.target.value),
                  })
                }
              />
            </label>
          ))}
          {repsSelect}
          <button
            className="primary"
            disabled={!ready || busy || !connected}
            onClick={runCase}
          >
            Ejecutar en las cuatro
          </button>
          <button
            className="text-button"
            disabled={busy}
            onClick={() =>
              openSql(
                structures
                  .flatMap((s) => testCase.sql(s.table, params))
                  .join("\n"),
              )
            }
          >
            Abrir en el editor
          </button>
        </div>
        <p className="panel-note">{testCase.note}</p>
        <pre className="sql-preview">
          {testCase.sql("org_bplus", params).join("\n")}
        </pre>
        {!ready && (
          <p className="panel-note">Primero crea las tablas de prueba.</p>
        )}

        {run && rows && runInfo && (
          <>
            <p className={`check ${same ? "ok" : "bad"}`}>
              {same
                ? runInfo.compare === "changes"
                  ? "✓ En las cuatro tablas la fila se insertó, se encontró, se eliminó y dejó de aparecer."
                  : `✓ Las cuatro tablas devolvieron el mismo resultado (${formatNumber(returned)} ${returned === 1 ? "fila" : "filas"})${run.reps > 1 ? ` en las ${run.reps} repeticiones` : ""}.`
                : "✗ Los resultados no coinciden entre tablas. Revisa el detalle de cada una."}
            </p>
            {/* las gráficas se rehacen con cada corrida: siempre muestran la última */}
            {chartRows?.length ? (
              <div className="chart-pair">
                <CompareChart
                  title="Páginas leídas"
                  unit="páginas"
                  format={(value) => formatNumber(value)}
                  logarithmic={
                    chartScale === "auto"
                      ? undefined
                      : chartScale === "log"
                  }
                  bars={chartRows.map((row) => {
                    const structure = structures.find(
                      (item) => item.id === row.id,
                    )!;
                    return {
                      id: row.id,
                      label: structure.label,
                      color: structure.color,
                      value: row.read,
                      note:
                        labels[row.step?.operacion ?? ""] ??
                        row.step?.operacion,
                    };
                  })}
                />
                <CompareChart
                  title={`Tiempo${run.reps > 1 ? " (mediana)" : ""}`}
                  unit="milisegundos"
                  format={(value) => formatNumber(value, value < 10 ? 2 : 0)}
                  logarithmic={
                    chartScale === "auto"
                      ? undefined
                      : chartScale === "log"
                  }
                  bars={chartRows.map((row) => {
                    const structure = structures.find(
                      (item) => item.id === row.id,
                    )!;
                    return {
                      id: row.id,
                      label: structure.label,
                      color: structure.color,
                      value: row.ms,
                    };
                  })}
                />
              </div>
            ) : (
              <p className="empty-small" role="status">
                Selecciona al menos una estructura para mostrar los gráficos.
              </p>
            )}
            <div className="table-scroll">
              <table>
                <thead>
                  <tr>
                    <th>Tabla</th>
                    <th>Acceso</th>
                    <th>Páginas leídas</th>
                    {runInfo.compare === "changes" && (
                      <th className="num">Páginas escritas</th>
                    )}
                    <th className="num">
                      Tiempo (ms){run.reps > 1 ? " · mediana" : ""}
                    </th>
                    <th />
                  </tr>
                </thead>
                <tbody>
                  {rows.map((row) => {
                    const s = structures.find((item) => item.id === row.id)!;
                    return (
                      <tr
                        key={row.id}
                        className={row.id === selected ? "current" : ""}
                      >
                        <td>
                          <span
                            className="swatch"
                            style={{ background: s.color }}
                          />
                          {s.label}
                        </td>
                        <td>
                          {row.step
                            ? (labels[row.step.operacion] ?? row.step.operacion)
                            : "—"}
                          {row.step?.indice && (
                            <small className="muted">
                              {" "}
                              · {row.step.indice}
                            </small>
                          )}
                        </td>
                        <td>
                          <span className="bar-cell">
                            <span className="inline-bar">
                              <span
                                style={{
                                  width: `${(row.read / maxRead) * 100}%`,
                                  background: s.color,
                                }}
                              />
                            </span>
                            {formatNumber(row.read)}
                          </span>
                        </td>
                        {runInfo.compare === "changes" && (
                          <td className="num">{formatNumber(row.written)}</td>
                        )}
                        <td className="num">{formatNumber(row.ms, 3)}</td>
                        <td>
                          <button
                            className="text-button"
                            aria-pressed={row.id === selected}
                            onClick={() => {
                              setSelected(row.id);
                              setStatement(0);
                            }}
                          >
                            Ver plan
                          </button>
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
            {detail && (
              <div className="detail">
                <div className="detail-heading">
                  <h3>{structures.find((s) => s.id === detail.id)!.table}</h3>
                  {detail.last.length > 1 && (
                    <div className="statement-tabs">
                      {detail.last.map((result, i) => (
                        <button
                          key={i}
                          aria-pressed={statement === i}
                          onClick={() => setStatement(i)}
                        >
                          {i + 1}. {result.tipo ?? "error"}
                        </button>
                      ))}
                    </div>
                  )}
                </div>
                <pre className="sql-preview">
                  {run.statements[detail.id][statement]}
                </pre>
                <div className="detail-grid">
                  <Plan result={detail.last[statement]} />
                  <Results
                    key={`${run.caseId}-${detail.id}-${statement}`}
                    result={detail.last[statement]}
                    busy={busy}
                  />
                </div>
              </div>
            )}
          </>
        )}
      </section>

      <section className="panel">
        <div className="panel-heading">
          <h2>B+ agrupado vs B+ no agrupado</h2>
          <div className="panel-actions">
            <button
              className="text-button"
              disabled={!sweep || busy}
              onClick={exportSweep}
            >
              Exportar CSV
            </button>
          </div>
        </div>
        <div className="toolbar">
          <code className="query-shape">
            SELECT COUNT(*) FROM tabla WHERE Index BETWEEN 1 AND N
          </code>
          {repsSelect}
          <button
            className="primary"
            disabled={!ready || busy || !connected}
            onClick={runSweep}
          >
            Medir con N creciente
          </button>
        </div>
        {sweep && lastPoint && (
          <div className="sweep">
            <div className="segmented" role="group" aria-label="Métrica">
              <button
                aria-pressed={metric === "pages"}
                onClick={() => setMetric("pages")}
              >
                Páginas leídas
              </button>
              <button
                aria-pressed={metric === "ms"}
                onClick={() => setMetric("ms")}
              >
                Tiempo (ms)
              </button>
            </div>
            {chartStructures.length ? (
              <RangeChart
                logarithmic={chartScale !== "linear"}
                series={structures
                  .filter((structure) =>
                    chartStructures.includes(structure.id),
                  )
                  .map((structure) => ({
                    id: structure.id,
                    label: structure.label,
                    color: structure.color,
                  }))}
                points={sweep.map((point) => ({
                  rows: point.rows,
                  values: Object.fromEntries(
                    structures
                      .filter((structure) =>
                        chartStructures.includes(structure.id),
                      )
                      .map((structure) => [
                        structure.id,
                        metric === "pages"
                          ? point.data[structure.id].pages
                          : point.data[structure.id].ms,
                      ]),
                  ),
                }))}
                yLabel={metric === "pages" ? "páginas leídas" : "tiempo (ms)"}
                format={(value) =>
                  metric === "pages"
                    ? formatNumber(value)
                    : `${formatNumber(value, 3)} ms`
                }
              />
            ) : (
              <p className="empty-small" role="status">
                Selecciona al menos una estructura para mostrar el gráfico.
              </p>
            )}
            <ul className="findings">
              <li>
                Con {formatNumber(lastPoint.rows)} filas el B+ agrupado leyó{" "}
                {formatNumber(lastPoint.data.bplus.pages)} páginas y el no
                agrupado {formatNumber(lastPoint.data.idx.pages)}. El agrupado
                tiene las filas juntas en sus hojas; el no agrupado va al heap
                por cada fila.
              </li>
              <li>
                {cross > 0
                  ? `El índice no agrupado deja de convenir entre ${formatNumber(sweep[cross - 1].rows)} y ${formatNumber(sweep[cross].rows)} filas (${percent(sweep[cross - 1].rows)} % a ${percent(sweep[cross].rows)} % de la tabla): desde ahí ${loses}.`
                  : cross === 0
                    ? `Desde el primer punto el índice no agrupado ${loses}.`
                    : `En ningún punto el índice no agrupado ${metric === "pages" ? "leyó más páginas" : "tardó más"} que el recorrido completo del heap.`}
              </li>
              <li>
                {countsMatch
                  ? "COUNT(*) coincide en las cuatro tablas en todos los puntos."
                  : "COUNT(*) no coincide en algún punto. Revisa la tabla."}
              </li>
            </ul>
            <div className="table-scroll">
              <table>
                <thead>
                  <tr>
                    <th className="num">N</th>
                    {structures.map((s) => (
                      <th key={s.id} className="num">
                        {s.label}
                      </th>
                    ))}
                    <th className="num">COUNT(*)</th>
                  </tr>
                </thead>
                <tbody>
                  {sweep.map((point) => (
                    <tr key={point.rows}>
                      <td className="num">{formatNumber(point.rows)}</td>
                      {structures.map((s) => (
                        <td key={s.id} className="num">
                          {metric === "pages"
                            ? formatNumber(point.data[s.id].pages)
                            : formatNumber(point.data[s.id].ms, 3)}
                        </td>
                      ))}
                      <td className="num">
                        {formatNumber(point.data.heap.count)}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        )}
      </section>
    </div>
  );
}
