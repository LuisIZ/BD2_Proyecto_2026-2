import { useEffect, useMemo, useState } from "react";
import {
  Circle,
  CircleMarker,
  MapContainer,
  Polygon,
  Popup,
  TileLayer,
  useMap,
} from "react-leaflet";
import L from "leaflet";
import "leaflet/dist/leaflet.css";
import { request } from "./api";
import type { Cell, Result, Table } from "./types";

interface PointRow {
  id: string;
  latitude: number;
  longitude: number;
  values: Cell[];
  columns: string[];
}

interface MapSelection {
  columns: string[];
  values: Cell[];
}

interface SearchShape {
  center?: [number, number];
  radius?: number;
  polygon?: [number, number][];
}

const pointPattern =
  /^\s*POINT\s*\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)\s*$/i;
const defaultCenter: [number, number] = [-12.0464, -77.0428];

function pointCoordinates(value: Cell): [number, number] | undefined {
  if (typeof value !== "string") return;
  const match = pointPattern.exec(value);
  if (!match) return;
  const latitude = Number(match[1]);
  const longitude = Number(match[2]);
  if (
    !Number.isFinite(latitude) ||
    !Number.isFinite(longitude) ||
    Math.abs(latitude) > 90 ||
    Math.abs(longitude) > 180
  )
    return;
  return [latitude, longitude];
}

function getSearchShape(sql: string): SearchShape {
  const shape: SearchShape = {};
  const radius = /distancia\s*\(\s*[\w.]+\s*,\s*POINT\s*\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)(?:\s*,\s*'[^']*')?\s*\)\s*<=?\s*(\d+(?:\.\d+)?)/i.exec(
    sql,
  );
  if (radius) {
    shape.center = [Number(radius[1]), Number(radius[2])];
    shape.radius = Number(radius[3]);
  }

  const polygon = /POLYGON\s*\(\s*\(\s*([^()]*)\)\s*\)/i.exec(sql);
  if (polygon) {
    const vertices = polygon[1]
      .split(",")
      .map((pair) =>
        pair
          .trim()
          .split(/\s+/)
          .map(Number),
      )
      .filter(
        (pair): pair is [number, number] =>
          pair.length === 2 &&
          pair.every(Number.isFinite) &&
          Math.abs(pair[0]) <= 90 &&
          Math.abs(pair[1]) <= 180,
      );
    if (vertices.length >= 3) shape.polygon = vertices;
  }
  return shape;
}

function queryResultRows(result?: Result) {
  return result?.ok ? (result.filas ?? []) : [];
}

function matchesRow(
  sourceColumns: string[],
  sourceValues: Cell[],
  candidateColumns: string[],
  candidateValues: Cell[],
) {
  let compared = 0;
  for (let index = 0; index < candidateColumns.length; index++) {
    const sourceIndex = sourceColumns.findIndex(
      (column) =>
        column.toLowerCase() === candidateColumns[index].toLowerCase(),
    );
    if (sourceIndex < 0) continue;
    compared++;
    if (sourceValues[sourceIndex] !== candidateValues[index]) return false;
  }
  return compared > 0;
}

function rowToken(
  values: Cell[],
  columns: string[],
  commonColumns: Array<[string, number, number]>,
) {
  return JSON.stringify(
    commonColumns.map(([name, sourceIndex, candidateIndex]) => [
      name,
      values[sourceIndex],
      columns[candidateIndex],
    ]),
  );
}

function MapContents({
  points,
  result,
  selectedRow,
  pointColumn,
  primaryKey,
  shape,
}: {
  points: PointRow[];
  result?: Result;
  selectedRow?: MapSelection;
  pointColumn: string;
  primaryKey: string;
  shape: SearchShape;
}) {
  const map = useMap();
  const pointRenderer = useMemo(() => L.canvas({ padding: 0.5 }), []);
  const resultColumns = result?.columnas ?? [];
  const resultRows = queryResultRows(result);
  const baseColumns = points[0]?.columns ?? [];
  const commonColumns = resultColumns.flatMap((column, resultIndex) => {
    const pointIndex = baseColumns.findIndex(
      (name) => name.toLowerCase() === column.toLowerCase(),
    );
    return pointIndex < 0
      ? []
      : [[column.toLowerCase(), pointIndex, resultIndex] as [string, number, number]];
  });
  const resultRowTokens = new Set(
    resultRows.map((row) => rowToken(row, resultColumns, commonColumns)),
  );
  const resultPointIndex = resultColumns.findIndex(
    (column) => column.toLowerCase() === pointColumn.toLowerCase(),
  );
  const resultKeyIndex = resultColumns.findIndex(
    (column) => column.toLowerCase() === primaryKey.toLowerCase(),
  );
  const resultCoordinates = new Set(
    resultRows
      .map((row) =>
        resultPointIndex >= 0 ? pointCoordinates(row[resultPointIndex]) : undefined,
      )
      .filter((point): point is [number, number] => point !== undefined)
      .map(([latitude, longitude]) => `${latitude},${longitude}`),
  );
  const resultKeys = new Set(
    resultKeyIndex < 0
      ? []
      : resultRows.map((row) => String(row[resultKeyIndex])),
  );
  const selectedPoint =
    selectedRow &&
    pointCoordinates(
      selectedRow.values[
        selectedRow.columns.findIndex(
          (column) => column.toLowerCase() === pointColumn.toLowerCase(),
        )
      ],
    );
  const selectedKeyIndex =
    selectedRow?.columns.findIndex(
      (column) => column.toLowerCase() === primaryKey.toLowerCase(),
    ) ?? -1;
  const selectedKey =
    selectedRow && selectedKeyIndex >= 0
      ? String(selectedRow.values[selectedKeyIndex])
      : undefined;
  const selectedRecord =
    selectedRow &&
    points.find((point) =>
      matchesRow(
        point.columns,
        point.values,
        selectedRow.columns,
        selectedRow.values,
      ),
    );
  const target = useMemo<[number, number] | undefined>(() => {
    if (selectedPoint) return selectedPoint;
    if (selectedKey !== undefined) {
      const point = points.find((item) => item.id === selectedKey);
      if (point) return [point.latitude, point.longitude];
    }
    if (selectedRecord)
      return [selectedRecord.latitude, selectedRecord.longitude];
  }, [selectedKey, selectedPoint?.[0], selectedPoint?.[1], selectedRecord]);

  useEffect(() => {
    if (target) {
      map.flyTo(target, Math.max(map.getZoom(), 13), { duration: 0.35 });
      return;
    }
    if (shape.center && shape.radius !== undefined) {
      map.fitBounds(L.latLng(shape.center).toBounds(shape.radius * 2));
      return;
    }
    const highlights = points.filter((point) => {
      const key = `${point.latitude},${point.longitude}`;
      return (
        resultCoordinates.has(key) ||
        resultKeys.has(point.id) ||
        (commonColumns.length > 0 &&
          resultRowTokens.has(
            rowToken(point.values, point.columns, commonColumns),
          ))
      );
    });
    if (highlights.length > 0)
      map.fitBounds(
        L.latLngBounds(
          highlights.map(
            (point) => [point.latitude, point.longitude] as [number, number],
          ),
        ).pad(0.2),
        { maxZoom: 13 },
      );
    else if (points.length > 0)
      map.fitBounds(
        L.latLngBounds(
          points.map(
            (point) => [point.latitude, point.longitude] as [number, number],
          ),
        ).pad(0.15),
        { maxZoom: 11 },
      );
  }, [
    map,
    points,
    resultCoordinates,
    resultKeys,
    resultRows,
    resultColumns,
    resultRowTokens,
    commonColumns,
    shape.center?.[0],
    shape.center?.[1],
    shape.radius,
    target,
  ]);

  return (
    <>
      <TileLayer
        attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
        url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
      />
      {points.map((point) => {
        const isSelected =
          (target &&
            point.latitude === target[0] &&
            point.longitude === target[1]) ||
          (selectedKey !== undefined && point.id === selectedKey);
        const isResult =
          resultCoordinates.has(`${point.latitude},${point.longitude}`) ||
          resultKeys.has(point.id) ||
          (commonColumns.length > 0 &&
            resultRowTokens.has(
              rowToken(point.values, point.columns, commonColumns),
            ));
        return (
          <CircleMarker
            key={point.id}
            center={[point.latitude, point.longitude]}
            radius={isSelected ? 9 : isResult ? 7 : 5}
            renderer={pointRenderer}
            pathOptions={{
              color: isSelected ? "#b6ff3b" : isResult ? "#ef7b35" : "#2a78d6",
              fillColor: isSelected ? "#b6ff3b" : isResult ? "#ef7b35" : "#2a78d6",
              fillOpacity: 0.85,
              weight: isSelected ? 3 : 1.5,
            }}
          >
            <Popup>
              <strong>{String(point.values[0] ?? `Registro ${point.id}`)}</strong>
              {point.values.map((value, index) => (
                <div key={index}>
                  {point.id === String(value) ? "" : String(value ?? "NULL")}
                </div>
              ))}
              <div>
                {point.latitude}, {point.longitude}
              </div>
            </Popup>
          </CircleMarker>
        );
      })}
      {shape.center && shape.radius !== undefined && (
        <Circle
          center={shape.center}
          radius={shape.radius}
          pathOptions={{
            color: "#b6ff3b",
            fillColor: "#b6ff3b",
            fillOpacity: 0.08,
            weight: 2,
          }}
        />
      )}
      {shape.polygon && (
        <Polygon
          positions={shape.polygon}
          pathOptions={{
            color: "#b6ff3b",
            fillColor: "#b6ff3b",
            fillOpacity: 0.1,
            weight: 2,
          }}
        />
      )}
    </>
  );
}

export default function SpatialMap({
  table,
  result,
  resultSql,
  selectedRow,
  busy,
}: {
  table?: Table;
  result?: Result;
  resultSql: string;
  selectedRow?: MapSelection;
  busy: boolean;
}) {
  const pointColumn = table?.columnas.find(
    (column) => column.tipo.toUpperCase() === "POINT",
  );
  const pointColumnName = pointColumn?.nombre;
  const [points, setPoints] = useState<PointRow[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");
  const shape = getSearchShape(resultSql);
  const tableName = table?.nombre;

  useEffect(() => {
    let current = true;
    if (!tableName || !pointColumnName) {
      setPoints([]);
      setError("");
      setLoading(false);
      return () => {
        current = false;
      };
    }
    const spatialColumn = pointColumnName;

    async function loadPoints() {
      setLoading(true);
      setError("");
      try {
        const data = await request<{ resultados: Result[] }>("consultas", {
          sql: `SELECT * FROM ${tableName};`,
        });
        const response = data.resultados[0];
        if (!response?.ok)
          throw new Error(response?.error ?? "No se pudieron cargar los puntos.");
        const columns = response.columnas ?? [];
        const pointIndex = columns.findIndex(
          (column) => column.toLowerCase() === spatialColumn.toLowerCase(),
        );
        const keyIndex = columns.findIndex(
          (column) => column.toLowerCase() === table?.clave.toLowerCase(),
        );
        if (pointIndex < 0)
          throw new Error(
            `La consulta no devolvió la columna espacial ${spatialColumn}.`,
          );
        const loaded = (response.filas ?? []).flatMap((values, index) => {
          const coordinates = pointCoordinates(values[pointIndex]);
          if (!coordinates) return [];
          const [latitude, longitude] = coordinates;
          return [
            {
              id: String(keyIndex < 0 ? index : values[keyIndex]),
              latitude,
              longitude,
              values,
              columns,
            },
          ];
        });
        if (current) setPoints(loaded);
      } catch (error) {
        if (current) {
          setPoints([]);
          setError((error as Error).message);
        }
      } finally {
        if (current) setLoading(false);
      }
    }

    void loadPoints();
    return () => {
      current = false;
    };
  }, [tableName, table?.clave, pointColumnName]);

  const center: [number, number] =
    points.length > 0
      ? [points[0].latitude, points[0].longitude]
      : shape.center ?? defaultCenter;
  const activeResult = result?.ok ? result : undefined;

  return (
    <section className="panel spatial-map-panel" aria-labelledby="map-title">
      <div className="panel-heading">
        <h2 id="map-title">Mapa espacial</h2>
        {pointColumn && table && (
          <span className="muted small">
            {table.nombre} · {pointColumn.nombre}
          </span>
        )}
      </div>
      {loading && (
        <p className="map-state" role="status">
          Cargando puntos del mapa…
        </p>
      )}
      {error && (
        <p className="message error map-state" role="alert">
          {error}
        </p>
      )}
      {!pointColumn ? (
        <p className="empty-small" role="status">
          Selecciona una tabla con una columna POINT para mostrar sus puntos.
        </p>
      ) : !loading && !error && points.length === 0 ? (
        <p className="empty-small" role="status">
          La tabla no contiene puntos geográficos válidos.
        </p>
      ) : (
        <>
          {busy && (
            <p className="map-state" role="status">
              Actualizando el resultado seleccionado…
            </p>
          )}
          {activeResult && !queryResultRows(activeResult).length && (
            <p className="map-state" role="status">
              La consulta no devolvió filas para resaltar.
            </p>
          )}
          <MapContainer
            className="spatial-map"
            center={center}
            zoom={points.length > 0 ? 11 : 12}
            scrollWheelZoom
          >
            <MapContents
              points={points}
              result={activeResult}
              selectedRow={selectedRow}
              pointColumn={pointColumn.nombre}
              primaryKey={table?.clave ?? ""}
              shape={shape}
            />
          </MapContainer>
          <div className="map-legend" aria-label="Leyenda del mapa">
            <span><i className="map-dot" /> Punto</span>
            <span><i className="map-dot result" /> Resultado</span>
            <span><i className="map-dot selected" /> Fila seleccionada</span>
            {shape.radius !== undefined && <span>Radio: {shape.radius} m</span>}
            {shape.polygon && <span>Polígono de búsqueda</span>}
            {selectedRow && <span role="status">Fila seleccionada en el mapa</span>}
            <span>{points.length.toLocaleString("es-PE")} puntos</span>
          </div>
        </>
      )}
    </section>
  );
}
