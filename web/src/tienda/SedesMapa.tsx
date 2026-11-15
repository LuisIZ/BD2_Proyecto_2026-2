import { useEffect, useState } from "react";
import {
  CircleMarker,
  MapContainer,
  Marker,
  Polyline,
  Popup,
  TileLayer,
  useMap,
  useMapEvents,
} from "react-leaflet";
import L from "leaflet";
import "leaflet/dist/leaflet.css";
import { knnLabel, queryColor } from "../SpatialMap";
import { obtenerSedes } from "./api";
import type { SedesCercanas } from "./types";

/** UTEC, en Barranco: la ubicación del usuario hasta que elija otra en el mapa. */
export const UBICACION_INICIAL = { lat: -12.1353, lon: -77.022 };

function Encuadre({ datos }: { datos: SedesCercanas }) {
  const map = useMap();
  useEffect(() => {
    const puntos: [number, number][] = [
      [datos.origen.lat, datos.origen.lon],
      ...datos.sedes.map((sede) => [sede.lat, sede.lon] as [number, number]),
    ];
    map.fitBounds(L.latLngBounds(puntos).pad(0.25), { maxZoom: 14 });
  }, [map, datos]);
  return null;
}

function AlHacerClic({ elegir }: { elegir: (lat: number, lon: number) => void }) {
  useMapEvents({
    click: (event) =>
      elegir(
        Number(event.latlng.lat.toFixed(4)),
        Number(event.latlng.lng.toFixed(4)),
      ),
  });
  return null;
}

const metros = (valor: number) =>
  valor < 1000
    ? `${valor.toLocaleString("es-PE")} m`
    : `${(valor / 1000).toLocaleString("es-PE", { maximumFractionDigits: 1 })} km`;

export default function SedesMapa({ id }: { id: number }) {
  const [ubicacion, setUbicacion] = useState(UBICACION_INICIAL);
  const [datos, setDatos] = useState<SedesCercanas>();
  const [cargando, setCargando] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => {
    let vigente = true;
    setCargando(true);
    setError("");
    obtenerSedes(id, ubicacion.lat, ubicacion.lon)
      .then((respuesta) => vigente && setDatos(respuesta.datos))
      .catch((e: Error) => vigente && setError(e.message))
      .finally(() => vigente && setCargando(false));
    return () => {
      vigente = false;
    };
  }, [id, ubicacion]);

  return (
    <section className="panel" aria-labelledby="sedes-titulo">
      <div className="panel-heading">
        <h2 id="sedes-titulo">Tiendas cercanas con stock</h2>
        <span className="muted small">Haz clic en el mapa para cambiar tu ubicación</span>
      </div>
      {error && (
        <p className="message error tienda-estado" role="alert">
          {error}
        </p>
      )}
      {cargando && (
        <p className="map-state" role="status">
          Buscando sedes…
        </p>
      )}
      {datos && !datos.sedes.length && (
        <p className="empty-small">Ninguna sede tiene stock de este producto.</p>
      )}
      {datos && datos.sedes.length > 0 && datos.sedes.length < datos.k && (
        <p className="empty-small">
          Solo {datos.sedes.length === 1 ? "1 sede tiene" : `${datos.sedes.length} sedes tienen`}{" "}
          stock de este producto.
        </p>
      )}
      <div className="tienda-mapa">
        <MapContainer
          className="spatial-map"
          center={[ubicacion.lat, ubicacion.lon]}
          zoom={13}
          scrollWheelZoom
        >
          <TileLayer
            attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
            url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
          />
          <AlHacerClic elegir={(lat, lon) => setUbicacion({ lat, lon })} />
          {datos && (
            <>
              <Encuadre datos={datos} />
              {datos.sedes.map((sede) => (
                <Polyline
                  key={`linea-${sede.id}`}
                  positions={[
                    [datos.origen.lat, datos.origen.lon],
                    [sede.lat, sede.lon],
                  ]}
                  pathOptions={{ color: queryColor, weight: 1.5, dashArray: "4 4" }}
                />
              ))}
              {datos.sedes.map((sede, i) => (
                <Marker
                  key={sede.id}
                  position={[sede.lat, sede.lon]}
                  icon={knnLabel(i + 1)}
                  title={`${i + 1}. ${sede.nombre}`}
                >
                  <Popup>
                    <strong>{sede.nombre}</strong>
                    <div>
                      {metros(sede.distancia_m)} · stock {sede.stock}
                    </div>
                  </Popup>
                </Marker>
              ))}
              <CircleMarker
                center={[datos.origen.lat, datos.origen.lon]}
                radius={8}
                pathOptions={{
                  color: "#ffffff",
                  fillColor: queryColor,
                  fillOpacity: 1,
                  weight: 2,
                }}
              >
                <Popup>Tu ubicación</Popup>
              </CircleMarker>
            </>
          )}
        </MapContainer>
        {datos && (
          <ol className="tienda-sedes" aria-label="Sedes cercanas">
            {datos.sedes.map((sede, i) => (
              <li key={sede.id}>
                <span className="knn-label tienda-numero">{i + 1}</span>
                <div>
                  <strong>{sede.nombre}</strong>
                  <span className="muted small">{sede.distrito}</span>
                  <span className="small">
                    {metros(sede.distancia_m)} · {sede.stock}{" "}
                    {sede.stock === 1 ? "unidad" : "unidades"}
                  </span>
                </div>
              </li>
            ))}
          </ol>
        )}
      </div>
      {datos && (
        <details className="tienda-sql">
          <summary>Consultas SQL</summary>
          {datos.consultas.map((consulta) => (
            <code key={consulta}>{consulta}</code>
          ))}
        </details>
      )}
    </section>
  );
}
