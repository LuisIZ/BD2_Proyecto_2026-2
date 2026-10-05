import { useCallback, useEffect, useMemo, useState } from "react";
import L from "leaflet";
import markerUrl from "leaflet/dist/images/marker-icon.png";
import markerRetinaUrl from "leaflet/dist/images/marker-icon-2x.png";
import markerShadowUrl from "leaflet/dist/images/marker-shadow.png";
import {
  Circle,
  MapContainer,
  Marker,
  Polygon,
  Popup,
  TileLayer,
  useMap,
  useMapEvents,
} from "react-leaflet";

type SqlResult = {
  ok: boolean;
  tipo?: string;
  mensaje?: string;
  error?: string;
  tiempo_ms?: number;
  columnas?: string[];
  filas?: Array<Array<string | number>>;
  plan?: Array<Record<string, string>>;
};
type CatalogTable = { nombre: string; organizacion: string; columnas: Array<{ nombre: string; tipo: string; pk: boolean }> };
type Point = { id: number; row: Array<string | number>; lat: number; lon: number; label: string };
type Overlay = { kind: "range"; lat: number; lon: number; radius: number } | { kind: "polygon"; vertices: Array<[number, number]> };

const DEFAULT_CENTER: [number, number] = [-12.0464, -77.0428];
const markerAssets = {
  iconUrl: markerUrl,
  iconRetinaUrl: markerRetinaUrl,
  shadowUrl: markerShadowUrl,
  iconSize: [25, 41] as [number, number],
  iconAnchor: [12, 41] as [number, number],
  popupAnchor: [1, -34] as [number, number],
  shadowSize: [41, 41] as [number, number],
};
const markerIcon = new L.Icon({ ...markerAssets });
const selectedIcon = new L.Icon({ ...markerAssets, className: "selected-marker" });
const matchIcon = new L.Icon({ ...markerAssets, className: "match-marker" });

async function api<T>(url: string, options?: RequestInit): Promise<T> {
  const response = await fetch(url, options);
  const body = await response.json();
  const detail = Array.isArray(body.detail) ? body.detail.map((item: { msg?: string }) => item.msg).join(". ") : body.detail;
  if (!response.ok || body.ok === false) throw new Error(body.error || body.mensaje || detail || `Error HTTP ${response.status}`);
  return body as T;
}

function distanceMeters(a: [number, number], b: [number, number]) {
  const radians = (degree: number) => degree * Math.PI / 180;
  const dLat = radians(b[0] - a[0]);
  const dLon = radians(b[1] - a[1]);
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(radians(a[0])) * Math.cos(radians(b[0])) * Math.sin(dLon / 2) ** 2;
  return 6_371_000 * 2 * Math.atan2(Math.sqrt(h), Math.sqrt(1 - h));
}

function insidePolygon(point: [number, number], vertices: Array<[number, number]>) {
  let inside = false;
  for (let i = 0, j = vertices.length - 1; i < vertices.length; j = i++) {
    const [yi, xi] = vertices[i];
    const [yj, xj] = vertices[j];
    const crosses = yi > point[0] !== yj > point[0] && point[1] < ((xj - xi) * (point[0] - yi)) / (yj - yi) + xi;
    if (crosses) inside = !inside;
  }
  return inside;
}

function FlyToSelection({ point }: { point?: Point }) {
  const map = useMap();
  useEffect(() => {
    if (point) map.flyTo([point.lat, point.lon], Math.max(map.getZoom(), 11), { duration: 0.7 });
  }, [map, point]);
  return null;
}

function FitPoints({ points }: { points: Point[] }) {
  const map = useMap();
  useEffect(() => {
    if (points.length > 1) {
      map.fitBounds(points.map((point) => [point.lat, point.lon] as [number, number]), { padding: [28, 28], maxZoom: 11 });
    } else if (points.length === 1) {
      map.setView([points[0].lat, points[0].lon], 11);
    }
  }, [map, points]);
  return null;
}

function DrawPolygon({ enabled, onVertex }: { enabled: boolean; onVertex: (vertex: [number, number]) => void }) {
  useMapEvents({
    click(event) {
      if (enabled) onVertex([event.latlng.lat, event.latlng.lng]);
    },
  });
  return null;
}

export default function App() {
  const [catalog, setCatalog] = useState<CatalogTable[]>([]);
  const [catalogState, setCatalogState] = useState<"loading" | "ready" | "error">("loading");
  const [catalogError, setCatalogError] = useState("");
  const [sql, setSql] = useState("SELECT * FROM ubicaciones_demo LIMIT 100;");
  const [queryBusy, setQueryBusy] = useState(false);
  const [queryError, setQueryError] = useState("");
  const [result, setResult] = useState<SqlResult | null>(null);
  const [plan, setPlan] = useState<Array<Record<string, string>>>([]);
  const [selectedId, setSelectedId] = useState<number>();
  const [latitudeColumn, setLatitudeColumn] = useState("");
  const [longitudeColumn, setLongitudeColumn] = useState("");
  const [scale, setScale] = useState("1");
  const [centerLat, setCenterLat] = useState(String(DEFAULT_CENTER[0]));
  const [centerLon, setCenterLon] = useState(String(DEFAULT_CENTER[1]));
  const [radiusKm, setRadiusKm] = useState("15");
  const [nearestCount, setNearestCount] = useState("3");
  const [matchedIds, setMatchedIds] = useState<number[]>([]);
  const [overlay, setOverlay] = useState<Overlay | null>(null);
  const [polygonDraft, setPolygonDraft] = useState<Array<[number, number]>>([]);
  const [drawing, setDrawing] = useState(false);
  const [demoBusy, setDemoBusy] = useState(false);

  const refreshCatalog = useCallback(async () => {
    setCatalogState("loading");
    try {
      const data = await api<{ tablas: CatalogTable[] }>("/api/catalogo");
      setCatalog(data.tablas || []);
      setCatalogState("ready");
      setCatalogError("");
    } catch (error) {
      setCatalogState("error");
      setCatalogError(error instanceof Error ? error.message : "No se pudo cargar el catálogo");
    }
  }, []);

  useEffect(() => { void refreshCatalog(); }, [refreshCatalog]);

  const rows = result?.filas ?? [];
  const columns = result?.columnas ?? [];
  const points = useMemo(() => {
    const latIndex = columns.indexOf(latitudeColumn);
    const lonIndex = columns.indexOf(longitudeColumn);
    const factor = Number(scale) || 1;
    return rows.flatMap((row, id) => {
      const pointValue = row.find((value) => /^POINT\s*\(/i.test(String(value)));
      const pointMatch = pointValue === undefined ? null : String(pointValue).match(/^POINT\s*\(\s*(-?(?:\d+(?:\.\d*)?|\.\d+))\s*,\s*(-?(?:\d+(?:\.\d*)?|\.\d+))\s*\)$/i);
      const coordinates: [number, number] | undefined = latIndex >= 0 && lonIndex >= 0
        ? [Number(row[latIndex]) / factor, Number(row[lonIndex]) / factor]
        : pointMatch ? [Number(pointMatch[1]), Number(pointMatch[2])] : undefined;
      if (!coordinates) return [];
      const [lat, lon] = coordinates;
      if (!Number.isFinite(lat) || !Number.isFinite(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180) return [];
      const label = String(row.find((value) => typeof value === "string" && !/^POINT\s*\(/i.test(value)) ?? `Registro ${id + 1}`);
      return [{ id, row, lat, lon, label }];
    });
  }, [columns, latitudeColumn, longitudeColumn, rows, scale]);
  const selectedPoint = points.find((point) => point.id === selectedId);
  const highlighted = new Set(matchedIds);

  const chooseColumn = (kind: "lat" | "lon", name: string) => {
    if (kind === "lat") setLatitudeColumn(name);
    else setLongitudeColumn(name);
    setScale(/e6|micro/i.test(name) ? "1000000" : "1");
  };

  async function executeQuery() {
    if (!sql.trim()) { setQueryError("Escribe una consulta SQL."); return; }
    setQueryBusy(true); setQueryError(""); setMatchedIds([]); setOverlay(null); setSelectedId(undefined);
    try {
      const data = await api<{ resultados: SqlResult[] }>("/api/consultas", {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ sql }),
      });
      const last = [...data.resultados].reverse().find((item) => item.columnas?.length);
      const failures = data.resultados.filter((item) => !item.ok);
      setPlan(data.resultados.flatMap((item) => item.plan ?? []));
      if (failures.length && !last) throw new Error(failures.map((item) => item.error).join(" · "));
      setResult(last ?? null);
      const newColumns = last?.columnas ?? [];
      const detectedLat = newColumns.find((name) => /lat/i.test(name)) ?? newColumns.find((name) => /^y$/i.test(name)) ?? "";
      const detectedLon = newColumns.find((name) => /lon|lng/i.test(name)) ?? newColumns.find((name) => /^x$/i.test(name)) ?? "";
      setLatitudeColumn(detectedLat); setLongitudeColumn(detectedLon);
      if (/e6|micro/i.test(`${detectedLat} ${detectedLon}`)) setScale("1000000");
      if (failures.length) setQueryError(failures.map((item) => item.error).join(" · "));
    } catch (error) {
      setResult(null); setPlan([]); setQueryError(error instanceof Error ? error.message : "Error al ejecutar SQL");
    } finally { setQueryBusy(false); }
  }

  async function loadDemo() {
    setDemoBusy(true); setQueryError("");
    try {
      const currentCatalog = await api<{ tablas: CatalogTable[] }>("/api/catalogo");
      if (currentCatalog.tablas.some((table) => table.nombre.toLowerCase() === "ubicaciones_demo")) {
        await api<{ resultados: SqlResult[] }>("/api/consultas", {
          method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ sql: "DROP TABLE ubicaciones_demo;" }),
        });
      }
      const demoSql = `CREATE TABLE ubicaciones_demo (id INT PRIMARY KEY, nombre VARCHAR(40), ubicacion POINT) USING HEAP;
    INSERT INTO ubicaciones_demo VALUES (1, 'Centro de Lima', POINT(-12.0464, -77.0428));
    INSERT INTO ubicaciones_demo VALUES (2, 'Miraflores', POINT(-12.1211, -77.0297));
    INSERT INTO ubicaciones_demo VALUES (3, 'San Isidro', POINT(-12.0970, -77.0365));
    INSERT INTO ubicaciones_demo VALUES (4, 'Barranco', POINT(-12.1492, -77.0209));
    INSERT INTO ubicaciones_demo VALUES (5, 'Callao', POINT(-12.0566, -77.1181));
    INSERT INTO ubicaciones_demo VALUES (6, 'Santiago de Surco', POINT(-12.1355, -76.9810));
    INSERT INTO ubicaciones_demo VALUES (7, 'La Molina', POINT(-12.0870, -76.9360));
    INSERT INTO ubicaciones_demo VALUES (8, 'Chorrillos', POINT(-12.1710, -77.0140));
    INSERT INTO ubicaciones_demo VALUES (9, 'Comas', POINT(-11.9570, -77.0480));
    INSERT INTO ubicaciones_demo VALUES (10, 'Los Olivos', POINT(-11.9760, -77.0760));
    INSERT INTO ubicaciones_demo VALUES (11, 'Pachacamac', POINT(-12.2300, -76.8600));
    INSERT INTO ubicaciones_demo VALUES (12, 'Huacho', POINT(-11.1067, -77.6050));
    SELECT * FROM ubicaciones_demo LIMIT 100;`;
      const data = await api<{ resultados: SqlResult[] }>("/api/consultas", {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ sql: demoSql }),
      });
      const demo = [...data.resultados].reverse().find((item) => item.columnas?.length);
      const failures = data.resultados.filter((item) => !item.ok);
      if (failures.length && !demo) throw new Error(failures.map((item) => item.error || item.mensaje).join(" · "));
      setSql("SELECT * FROM ubicaciones_demo LIMIT 100;");
      setResult(demo ?? null); setPlan(data.resultados.flatMap((item) => item.plan ?? [])); setLatitudeColumn(""); setLongitudeColumn("");
      setScale("1"); setMatchedIds([]); setOverlay(null); setSelectedId(undefined);
      if (failures.length) setQueryError(failures.map((item) => item.error || item.mensaje).join(" · "));
      await refreshCatalog();
    } catch (error) { setQueryError(error instanceof Error ? error.message : "No se pudo cargar el dataset de prueba"); }
    finally { setDemoBusy(false); }
  }

  function applyRange() {
    const lat = Number(centerLat), lon = Number(centerLon), radius = Number(radiusKm) * 1000;
    if (!validCenter(lat, lon) || !Number.isFinite(radius) || radius <= 0) { setQueryError("Centro o radio no válido."); return; }
    const matches = points.filter((point) => distanceMeters([lat, lon], [point.lat, point.lon]) <= radius);
    setMatchedIds(matches.map((point) => point.id)); setOverlay({ kind: "range", lat, lon, radius }); setQueryError("");
  }

  function applyKnn() {
    const lat = Number(centerLat), lon = Number(centerLon), k = Number(nearestCount);
    if (!validCenter(lat, lon) || !Number.isInteger(k) || k < 1) { setQueryError("Centro o valor k no válido."); return; }
    const nearest = [...points].sort((a, b) => distanceMeters([lat, lon], [a.lat, a.lon]) - distanceMeters([lat, lon], [b.lat, b.lon])).slice(0, k);
    setMatchedIds(nearest.map((point) => point.id)); setOverlay(null); setQueryError("");
  }

  function finishPolygon() {
    if (polygonDraft.length < 3) { setQueryError("El polígono necesita al menos 3 vértices; haz clic sobre el mapa."); return; }
    setMatchedIds(points.filter((point) => insidePolygon([point.lat, point.lon], polygonDraft)).map((point) => point.id));
    setOverlay({ kind: "polygon", vertices: polygonDraft }); setDrawing(false); setQueryError("");
  }

  function clearSpatial() {
    setMatchedIds([]); setOverlay(null); setPolygonDraft([]); setDrawing(false); setQueryError("");
  }

  const setPointSelection = (id: number) => setSelectedId(id);

  return (
    <div className="app-shell">
      <header className="topbar">
        <div className="brand-mark">BD</div>
        <div className="brand-copy"><strong>Minigestor</strong><span>Base de Datos 2 · laboratorio espacial</span></div>
        <div className="topbar-right"><span className="live-dot" /> {catalogState === "ready" ? `${catalog.length} tablas disponibles` : "Conectando con el motor"}</div>
      </header>

      <main className="workspace">
        <section className="panel files-panel">
          <PanelTitle number="01" title="Archivos" subtitle="Catálogo de tablas" />
          <button className="demo-button" onClick={() => void loadDemo()} disabled={demoBusy}>
            <span>✦</span> {demoBusy ? "Preparando dataset…" : "Cargar datos de prueba"}
          </button>
          <div className="table-list">
            {catalogState === "loading" && <StateMessage title="Conectando…" text="Consultando el catálogo del motor." />}
            {catalogState === "error" && <StateMessage title="No se pudo conectar" text={catalogError} error />}
            {catalogState === "ready" && catalog.length === 0 && <StateMessage title="Catálogo vacío" text="Carga el dataset de prueba o crea una tabla desde SQL." />}
            {catalog.map((table) => <button className="table-card" key={table.nombre} onClick={() => setSql(`SELECT * FROM ${table.nombre} LIMIT 100;`)}>
              <span className="table-icon">▦</span><span className="table-info"><strong>{table.nombre}</strong><small>{table.columnas.length} columnas · {table.organizacion}</small></span><span className="chevron">›</span>
            </button>)}
          </div>
          <div className="side-note"><span className="note-icon">i</span><span>Selecciona una tabla para generar una consulta. Los nombres de columnas de coordenadas se detectan automáticamente.</span></div>
        </section>

        <div className="center-column">
          <section className="panel query-panel">
            <PanelTitle number="02" title="Consultas" subtitle="Editor SQL" />
            <div className="editor-wrap"><div className="editor-gutter">1<br />2</div><textarea aria-label="Editor SQL" spellCheck={false} value={sql} onChange={(event) => setSql(event.target.value)} onKeyDown={(event) => { if ((event.ctrlKey || event.metaKey) && event.key === "Enter") { event.preventDefault(); void executeQuery(); } }} /></div>
            <div className="query-footer"><span className="shortcut">Ctrl + Enter para ejecutar</span><button className="run-button" onClick={() => void executeQuery()} disabled={queryBusy}><span>{queryBusy ? "◌" : "▶"}</span>{queryBusy ? "Ejecutando…" : "Ejecutar SQL"}</button></div>
            {queryError && <div className="inline-error" role="alert">{queryError}</div>}
          </section>

          <section className="panel results-panel">
            <div className="section-heading"><PanelTitle number="03" title="Resultados" subtitle={result ? `${rows.length} filas · ${result.tiempo_ms?.toFixed(2) ?? "—"} ms` : "Salida de consulta"} /><button className="icon-button" title="Actualizar catálogo" onClick={() => void refreshCatalog()}>↻</button></div>
            {queryBusy ? <StateMessage title="Ejecutando consulta…" text="Esperando respuesta del motor." /> : !result ? <StateMessage title="Sin resultados todavía" text="Ejecuta una consulta SQL o carga el dataset de prueba." /> : columns.length === 0 || rows.length === 0 ? <StateMessage title="Consulta sin filas" text={result.mensaje || "La consulta se ejecutó correctamente, pero no devolvió registros."} /> : <div className="results-scroll"><table><thead><tr>{columns.map((column) => <th key={column}>{column}</th>)}</tr></thead><tbody>{rows.map((row, index) => <tr key={index} onClick={() => setPointSelection(index)} className={`${selectedId === index ? "selected-row" : ""} ${highlighted.has(index) ? "matched-row" : ""}`} title="Selecciona para centrar en el mapa">{row.map((value, cell) => <td key={cell}>{String(value)}</td>)}</tr>)}</tbody></table></div>}
            {matchedIds.length > 0 && <div className="match-footer"><span className="match-dot" /> {matchedIds.length} punto{matchedIds.length === 1 ? "" : "s"} coinciden con la búsqueda espacial</div>}
          </section>

          <section className="panel plan-panel">
            <PanelTitle number="04" title="Plan" subtitle={plan.length ? `${plan.length} pasos de ejecución` : "Plan del motor"} />
            {plan.length ? <div className="plan-list">{plan.map((step, index) => <div className="plan-step" key={`${step.operacion}-${index}`}><span className="step-index">{String(index + 1).padStart(2, "0")}</span><strong>{step.operacion}</strong><span>{Object.entries(step).filter(([key]) => key !== "operacion").map(([key, value]) => `${key}: ${value}`).join(" · ")}</span></div>)}</div> : <div className="empty-plan">El detalle del plan aparecerá al ejecutar SQL.</div>}
          </section>
        </div>

        <section className="panel map-panel">
          <div className="map-heading"><PanelTitle number="05" title="Mapa interactivo" subtitle="Explora y filtra coordenadas" /><span className="map-badge">LEAFLET</span></div>
          <div className="spatial-toolbar">
            <label>Latitud<select value={latitudeColumn} onChange={(event) => chooseColumn("lat", event.target.value)}><option value="">Seleccionar columna</option>{columns.map((column) => <option key={column}>{column}</option>)}</select></label>
            <label>Longitud<select value={longitudeColumn} onChange={(event) => chooseColumn("lon", event.target.value)}><option value="">Seleccionar columna</option>{columns.map((column) => <option key={column}>{column}</option>)}</select></label>
            <label>Escala<select value={scale} onChange={(event) => setScale(event.target.value)}><option value="1">Grados (1)</option><option value="1000">Milésimas (1 000)</option><option value="1000000">Microgrados (1 000 000)</option></select></label>
          </div>
          <div className="spatial-controls">
            <div className="coordinate-inputs"><label>Centro lat.<input type="number" step="any" value={centerLat} onChange={(event) => setCenterLat(event.target.value)} /></label><label>Centro lon.<input type="number" step="any" value={centerLon} onChange={(event) => setCenterLon(event.target.value)} /></label></div>
            <div className="operation-row"><label className="compact-input">Radio km<input type="number" min="0.1" step="0.5" value={radiusKm} onChange={(event) => setRadiusKm(event.target.value)} /></label><button className="spatial-button" onClick={applyRange} disabled={!points.length}>Rango</button><label className="compact-input">k<input type="number" min="1" step="1" value={nearestCount} onChange={(event) => setNearestCount(event.target.value)} /></label><button className="spatial-button secondary" onClick={applyKnn} disabled={!points.length}>k-NN</button></div>
            <div className="polygon-row"><button className={`spatial-button polygon-button ${drawing ? "active" : ""}`} onClick={() => { setPolygonDraft([]); setDrawing(true); setOverlay(null); setMatchedIds([]); }}>⌁ {drawing ? "Dibujando…" : "Dibujar polígono"}</button>{drawing && <button className="finish-button" onClick={finishPolygon}>Aplicar ({polygonDraft.length})</button>}<button className="clear-button" onClick={clearSpatial}>Limpiar</button></div>
          </div>
          <div className="map-caption">{drawing ? "Haz clic en el mapa para marcar vértices; aplica con 3 o más." : points.length ? `${points.length} puntos · ${matchedIds.length ? `${matchedIds.length} resaltados` : "sin filtro espacial"}` : "Se admiten columnas POINT o latitud/longitud numéricas."}</div>
          <div className="map-frame">
            {queryBusy ? <div className="map-empty map-loading" role="status"><div className="map-empty-icon">◌</div><strong>Actualizando el mapa…</strong><span>Esperando las filas de la consulta.</span></div> : points.length ? <MapContainer center={points[0] ? [points[0].lat, points[0].lon] : DEFAULT_CENTER} zoom={points.length > 1 ? 5 : 11} scrollWheelZoom className={drawing ? "drawing-map" : ""}>
              <TileLayer attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>' url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png" />
              <DrawPolygon enabled={drawing} onVertex={(vertex) => setPolygonDraft((previous) => [...previous, vertex])} />
              <FitPoints points={points} />
              <FlyToSelection point={selectedPoint} />
              {overlay?.kind === "range" && <><Circle center={[overlay.lat, overlay.lon]} radius={overlay.radius} pathOptions={{ color: "#4568e8", fillColor: "#6d83f2", fillOpacity: 0.1, weight: 2 }} /><Marker position={[overlay.lat, overlay.lon]} icon={markerIcon}><Popup>Centro de búsqueda · radio {(overlay.radius / 1000).toFixed(1)} km</Popup></Marker></>}
              {overlay?.kind === "polygon" && <Polygon positions={overlay.vertices} pathOptions={{ color: "#9b5de5", fillColor: "#b28bea", fillOpacity: 0.16, weight: 2 }} />}
              {drawing && polygonDraft.length > 0 && <Polygon positions={polygonDraft} pathOptions={{ color: "#9b5de5", dashArray: "5 7", fillOpacity: 0.07 }} />}
              {points.map((point) => <Marker key={point.id} position={[point.lat, point.lon]} icon={selectedId === point.id ? selectedIcon : highlighted.has(point.id) ? matchIcon : markerIcon} eventHandlers={{ click: (event) => { if (drawing) setPolygonDraft((previous) => [...previous, [event.latlng.lat, event.latlng.lng]]); else setPointSelection(point.id); } }} opacity={matchedIds.length && !highlighted.has(point.id) ? 0.35 : 1} zIndexOffset={highlighted.has(point.id) ? 800 : selectedId === point.id ? 1000 : 0}>
                <Popup><strong>{point.label}</strong><br />{point.lat.toFixed(5)}, {point.lon.toFixed(5)}{highlighted.has(point.id) && <><br /><span className="popup-match">Coincide con la búsqueda</span></>}</Popup>
              </Marker>)}
            </MapContainer> : <div className={`map-empty ${queryError ? "map-error" : ""}`} role={queryError ? "alert" : "status"}><div className="map-empty-icon">{queryError ? "!" : "⌖"}</div><strong>{queryError ? "No se pudo actualizar el mapa" : result && rows.length === 0 ? "Consulta sin puntos" : result ? "No hay coordenadas para mostrar" : "Tu mapa está listo"}</strong><span>{queryError || (result && rows.length === 0 ? "La consulta terminó correctamente, pero no devolvió filas." : result ? "Incluye un campo POINT o selecciona columnas numéricas de latitud y longitud." : "Ejecuta SQL con coordenadas o carga los datos de prueba.")}</span></div>}
          </div>
          <div className="map-legend"><span><i className="legend-point" /> Registro</span><span><i className="legend-match" /> Coincidencia</span>{overlay?.kind === "range" && <span><i className="legend-range" /> Radio de búsqueda</span>}{overlay?.kind === "polygon" && <span><i className="legend-polygon" /> Polígono consultado</span>}</div>
        </section>
      </main>
      <footer className="app-footer"><span>Motor SQL BD2</span><span>Las operaciones espaciales se calculan sobre las filas visibles de la consulta actual.</span><span><kbd>Ctrl</kbd> + <kbd>Enter</kbd> ejecutar</span></footer>
    </div>
  );
}

function validCenter(lat: number, lon: number) { return Number.isFinite(lat) && Number.isFinite(lon) && Math.abs(lat) <= 90 && Math.abs(lon) <= 180; }

function PanelTitle({ number, title, subtitle }: { number: string; title: string; subtitle: string }) {
  return <div className="panel-title"><span className="panel-number">{number}</span><div><h2>{title}</h2><p>{subtitle}</p></div></div>;
}

function StateMessage({ title, text, error = false }: { title: string; text: string; error?: boolean }) {
  return <div className={`state-message ${error ? "state-error" : ""}`}><span className="state-symbol">{error ? "!" : "·"}</span><strong>{title}</strong><span>{text}</span></div>;
}