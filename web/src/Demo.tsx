import { useEffect, useMemo, useRef, useState } from "react";
import { request } from "./api";
import type { Result } from "./types";
import { Plan, Results, labels } from "./Results";
import { demoScript, demoSections } from "./demoScript";
import { accessStep, formatNumber, pagesRead } from "./structures";
import { datasetLabel, datasetRows } from "./sql";

type Status = "pending" | "running" | "ok" | "skipped" | "expected" | "error";

function statusOf(
  step: { cleanup?: boolean; expectError?: boolean },
  result?: Result,
): Status {
  if (!result) return "pending";
  if (result.ok) return step.expectError ? "error" : "ok";
  if (step.expectError) return "expected";
  return step.cleanup && /no existe/i.test(result.error ?? "")
    ? "skipped"
    : "error";
}

function summary(result: Result) {
  if (!result.ok) return result.error ?? "error";
  const parts = [result.mensaje || `${result.filas?.length ?? 0} filas`];
  const step = accessStep([result]);
  if (step) {
    const pages = pagesRead([result]);
    parts.push(
      `${labels[step.operacion] ?? step.operacion} · ${formatNumber(pages)} ${pages === 1 ? "página" : "páginas"}`,
    );
  }
  parts.push(`${formatNumber(result.tiempo_ms ?? 0, 2)} ms`);
  return parts.join(" · ");
}

export default function Demo({
  active,
  connected,
  refresh,
  openSql,
}: {
  active: boolean;
  connected: boolean;
  refresh: () => Promise<void>;
  openSql: (sql: string) => void;
}) {
  const [datasets, setDatasets] = useState<string[]>([]);
  const [csv, setCsv] = useState("organizations-1000.csv");
  const [results, setResults] = useState<Record<number, Result>>({});
  const [selected, setSelected] = useState<number>();
  const [running, setRunning] = useState<number>();
  const [error, setError] = useState("");
  const busy = running !== undefined;
  const inFlight = useRef(false);
  const list = useRef<HTMLOListElement>(null);

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

  const sections = useMemo(() => demoSections(csv), [csv]);
  const steps = useMemo(
    () =>
      sections.flatMap((section, s) =>
        section.steps.map((step, i) => ({ ...step, section: s, first: i === 0 })),
      ),
    [sections],
  );
  const done = steps.filter((_, i) => results[i] !== undefined).length;
  const next = steps.findIndex((_, i) => results[i] === undefined);
  const finished = next === -1;

  function reset(nextCsv = csv) {
    setCsv(nextCsv);
    setResults({});
    setSelected(undefined);
    setError("");
  }

  async function run(index: number) {
    if (inFlight.current || !connected) return;
    inFlight.current = true;
    setRunning(index);
    setSelected(index);
    setError("");
    let result: Result;
    try {
      const data = await request<{ resultados: Result[] }>("consultas", {
        sql: steps[index].sql,
      });
      result = data.resultados[0] ?? { ok: false, error: "Sin respuesta." };
    } catch (error) {
      result = { ok: false, error: (error as Error).message };
    }
    setResults((previous) => ({ ...previous, [index]: result }));
    setRunning(undefined);
    inFlight.current = false;
    await refresh();
    list.current
      ?.querySelector(`[data-step="${index + 1}"]`)
      ?.scrollIntoView({ block: "nearest" });
  }

  const runNext = () => {
    if (!finished) void run(next);
  };

  useEffect(() => {
    if (!active) return;
    function onKey(event: KeyboardEvent) {
      if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
        event.preventDefault();
        runNext();
      }
    }
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  });

  const current = selected !== undefined ? results[selected] : undefined;

  return (
    <div className="demo">
      {error && (
        <p className="message error" role="alert">
          {error}
        </p>
      )}
      <section className="panel">
        <div className="panel-heading">
          <h2>Demo guiada</h2>
          <div className="panel-actions">
            <div className="segmented" role="group" aria-label="Tamaño del dataset">
              {(datasets.length ? datasets : [csv]).map((item) => (
                <button
                  key={item}
                  aria-pressed={csv === item}
                  disabled={busy}
                  title={item}
                  onClick={() => reset(item)}
                >
                  {datasetLabel(item).split(" · ")[0]}
                </button>
              ))}
            </div>
            <button
              className="text-button"
              disabled={busy}
              onClick={() => openSql(demoScript(csv))}
            >
              Abrir guion en el editor
            </button>
            <button disabled={busy || !done} onClick={() => reset()}>
              Reiniciar
            </button>
            <button
              className="primary"
              disabled={busy || !connected || finished}
              onClick={runNext}
            >
              {busy
                ? "Ejecutando…"
                : finished
                  ? "Demo completa"
                  : `Ejecutar paso ${next + 1}`}
            </button>
          </div>
        </div>
        <p className="panel-note">
          Carga <code>{csv}</code> ({formatNumber(datasetRows(csv))} filas) en
          Heap, Secuencial y B+ agrupado y recorre el guion paso por paso.{" "}
          <kbd>Ctrl + Enter</kbd> ejecuta el siguiente paso; cada paso también
          se puede ejecutar por separado o repetir.
        </p>
        <div className="demo-progress" role="status">
          <progress value={done} max={steps.length} />
          <span>
            {done} de {steps.length} pasos
          </span>
        </div>
        <div className="demo-layout">
          <ol className="demo-steps" ref={list} aria-label="Pasos de la demo">
            {steps.map((step, i) => {
              const status = statusOf(step, results[i]);
              const isNext = i === next && !busy;
              return (
                <li key={i} className={step.first ? "section-start" : ""}>
                  {step.first && (
                    <div className="demo-section">
                      <h3>
                        {step.section}. {sections[step.section].title}
                      </h3>
                      <p>{sections[step.section].description}</p>
                    </div>
                  )}
                  <div
                    className={`demo-step ${i === running ? "running" : status} ${i === selected ? "current" : ""}`}
                    data-step={i + 1}
                    aria-current={isNext ? "step" : undefined}
                  >
                    <span className="step-number">{i + 1}</span>
                    <button
                      className="demo-body"
                      onClick={() => setSelected(i)}
                      disabled={results[i] === undefined}
                      aria-label={`Ver resultado del paso ${i + 1}`}
                    >
                      <code className="demo-sql">{step.sql}</code>
                      {step.note && <small className="muted">{step.note}</small>}
                      {results[i] && (
                        <small className={`demo-result ${status}`}>
                          {status === "skipped"
                            ? "no existía · nada que borrar"
                            : status === "expected"
                              ? `rechazado como se esperaba: ${results[i].error}`
                              : status === "error" && results[i].ok
                                ? `debía fallar y no falló · ${summary(results[i])}`
                                : summary(results[i])}
                        </small>
                      )}
                    </button>
                    <button
                      className={isNext ? "primary" : ""}
                      disabled={busy || !connected}
                      onClick={() => void run(i)}
                    >
                      {i === running
                        ? "…"
                        : results[i] !== undefined
                          ? "Repetir"
                          : "Ejecutar"}
                    </button>
                  </div>
                </li>
              );
            })}
          </ol>
          <div className="demo-detail">
            {selected !== undefined && (
              <p className="demo-selected">
                Paso {selected + 1}
                <code>{steps[selected].sql}</code>
              </p>
            )}
            <Results
              key={`${selected}-${current?.tiempo_ms}`}
              result={current}
              busy={busy}
            />
            <Plan result={current} />
          </div>
        </div>
      </section>
    </div>
  );
}
