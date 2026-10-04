import { useEffect, useRef, useState } from "react";
import type { FormEvent } from "react";
import { request } from "./api";
import type { Result } from "./types";
import { datasetLabel, datasetRows } from "./sql";

/** Columna del CSV tal como la infiere la API antes de cargar la tabla. */
interface Column {
  nombre: string;
  original: string;
  tipo: "INT" | "VARCHAR";
  ancho: number;
  /** entera y sin valores repetidos en la muestra leída */
  unica: boolean;
}

export default function ImportCsv({
  close,
  imported,
}: {
  close: () => void;
  imported: (results: Result[], name: string) => void;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  const [datasets, setDatasets] = useState<string[]>([]);
  const [dataset, setDataset] = useState("organizations-1000.csv");
  const [source, setSource] = useState("proyecto");
  const [file, setFile] = useState<File>();
  const [name, setName] = useState("organizaciones");
  const [organization, setOrganization] = useState("HEAP");
  const [columns, setColumns] = useState<Column[]>([]);
  const [key, setKey] = useState("");
  const [indexes, setIndexes] = useState<string[]>([]);
  const [reading, setReading] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  useEffect(() => {
    dialog.current?.showModal();
    request<{ archivos: string[] }>("datasets")
      .then((data) => {
        setDatasets(
          [...data.archivos].sort((a, b) => datasetRows(a) - datasetRows(b)),
        );
        setDataset(
          data.archivos.includes("organizations-1000.csv")
            ? "organizations-1000.csv"
            : (data.archivos[0] ?? ""),
        );
      })
      .catch((error) => setError(error.message));
  }, []);

  // al cambiar de archivo se leen sus columnas para poder elegir clave e índices
  useEffect(() => {
    const pending = source === "local" ? file : undefined;
    const chosen = source === "proyecto" ? dataset : "";
    if (!pending && !chosen) {
      setColumns([]);
      return;
    }
    let cancelled = false;
    setReading(true);
    setError("");
    (async () =>
      request<{ columnas: Column[] }>(
        "columnas",
        pending ? { contenido: await pending.text() } : { dataset: chosen },
      ))()
      .then((data) => {
        if (cancelled) return;
        setColumns(data.columnas);
        // el motor toma la primera INT; se propone la primera que además no repite
        const candidate =
          data.columnas.find((c) => c.unica) ??
          data.columnas.find((c) => c.tipo === "INT");
        setKey(candidate?.nombre ?? "");
        setIndexes([]);
      })
      .catch((error) => {
        if (!cancelled) {
          setColumns([]);
          setError((error as Error).message);
        }
      })
      .finally(() => !cancelled && setReading(false));
    return () => {
      cancelled = true;
    };
  }, [source, dataset, file]);

  const integers = columns.filter((column) => column.tipo === "INT");
  const canIndex = organization === "HEAP";
  const toggleIndex = (name: string) =>
    setIndexes((current) =>
      current.includes(name)
        ? current.filter((item) => item !== name)
        : [...current, name],
    );

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError("");
    if (source === "local" && !file) {
      setError("Selecciona un archivo CSV.");
      return;
    }
    if (file && source === "local" && file.size > 32 * 1024 * 1024) {
      setError("El límite de carga es 32 MB.");
      return;
    }
    setBusy(true);
    try {
      const body = {
        nombre: name,
        organizacion: organization,
        clave: key || null,
        indices: canIndex ? indexes : [],
        ...(source === "local"
          ? { contenido: await file!.text() }
          : { dataset }),
      };
      const data = await request<{ resultados: Result[] }>("importar", body);
      const failure = data.resultados.find((result) => !result.ok);
      if (failure) throw new Error(failure.error);
      imported(data.resultados, name);
      close();
    } catch (error) {
      setError((error as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <dialog
      ref={dialog}
      onCancel={(event) => {
        event.preventDefault();
        if (!busy) close();
      }}
      aria-labelledby="import-title"
    >
      <form onSubmit={submit}>
        <div className="dialog-heading">
          <h2 id="import-title">Cargar una tabla</h2>
          <button
            type="button"
            onClick={close}
            disabled={busy}
            aria-label="Cerrar"
          >
            ×
          </button>
        </div>
        <p className="muted">
          El CSV debe incluir encabezados y una columna de enteros para la clave
          primaria.
        </p>
        <fieldset disabled={busy}>
          <legend className="sr-only">Datos de importación</legend>
          <label>
            Origen
            <select
              value={source}
              onChange={(event) => setSource(event.target.value)}
            >
              <option value="proyecto">Archivos del proyecto</option>
              <option value="local">Archivo de mi equipo</option>
            </select>
          </label>
          {source === "proyecto" ? (
            <label>
              Archivo CSV
              <select
                value={dataset}
                onChange={(event) => setDataset(event.target.value)}
                required
              >
                {!datasets.length && (
                  <option value="">No hay archivos disponibles</option>
                )}
                {datasets.map((item) => (
                  <option key={item} value={item}>
                    {datasetLabel(item)}
                  </option>
                ))}
              </select>
            </label>
          ) : (
            <label>
              Archivo CSV
              <input
                type="file"
                accept=".csv,text/csv"
                onChange={(event) => setFile(event.target.files?.[0])}
                required
              />
              <small>UTF-8, separado por comas. Máximo 32 MB.</small>
            </label>
          )}
          <label>
            Nombre de la tabla
            <input
              value={name}
              onChange={(event) => setName(event.target.value)}
              pattern="[A-Za-z_][A-Za-z0-9_]{0,49}"
              title="Usa letras sin tildes, números y guion bajo; empieza con una letra o guion bajo."
              required
              maxLength={50}
            />
          </label>
          <label>
            Organización del archivo
            <select
              value={organization}
              onChange={(event) => setOrganization(event.target.value)}
            >
              <option value="HEAP">Heap File</option>
              <option value="SEQUENTIAL">Secuencial paginado</option>
              <option value="BPLUS">B+ agrupado</option>
            </select>
          </label>
          <label>
            Clave primaria
            <select
              value={key}
              onChange={(event) => setKey(event.target.value)}
              disabled={!integers.length}
            >
              {!integers.length && (
                <option value="">
                  {reading ? "Leyendo el archivo…" : "Sin columnas INT"}
                </option>
              )}
              {integers.map((column) => (
                <option key={column.nombre} value={column.nombre}>
                  {column.nombre}
                  {column.unica ? " · sin repetidos" : " · tiene repetidos"}
                </option>
              ))}
            </select>
            <small>
              Debe ser INT y no repetirse. El B+ agrupado y el secuencial
              ordenan el archivo por esta columna.
            </small>
          </label>
          {integers.length > 1 && (
            <fieldset className="checks" disabled={!canIndex}>
              <legend>Índices B+ no agrupados</legend>
              {integers
                .filter((column) => column.nombre !== key)
                .map((column) => (
                  <label key={column.nombre} className="check">
                    <input
                      type="checkbox"
                      checked={indexes.includes(column.nombre)}
                      onChange={() => toggleIndex(column.nombre)}
                    />
                    {column.nombre}
                  </label>
                ))}
              <small>
                {canIndex
                  ? "Aceleran las búsquedas por esa columna a cambio de espacio y de mantenimiento en cada INSERT."
                  : "Solo se admiten sobre Heap: las otras organizaciones reubican los registros."}
              </small>
            </fieldset>
          )}
          {!!columns.length && (
            <p className="hint">
              {columns.length} columnas detectadas · {integers.length} de tipo
              INT
            </p>
          )}
        </fieldset>
        {error && (
          <p className="message error" role="alert">
            {error}
          </p>
        )}
        <div className="dialog-actions">
          <button type="button" onClick={close} disabled={busy}>
            Cancelar
          </button>
          <button className="primary" disabled={busy}>
            {busy ? "Cargando…" : "Cargar tabla"}
          </button>
        </div>
      </form>
    </dialog>
  );
}
