import { useEffect, useState } from "react";
import { Thumbnail } from "../Gallery";
import { buscarProductos, precio } from "./api";
import type { Catalogo as CatalogoDatos, ProductoResumen } from "./types";

/** Tarjeta con foto, nombre, marca y precio; la usan el catálogo y las recomendaciones. */
export function Tarjeta({
  producto,
  abrir,
  children,
}: {
  producto: ProductoResumen;
  abrir: (id: number) => void;
  children?: React.ReactNode;
}) {
  return (
    <li className="tienda-tarjeta">
      <button
        className="tienda-tarjeta-abrir"
        onClick={() => abrir(producto.id)}
        aria-label={`Ver ${producto.nombre}`}
      >
        <Thumbnail id={producto.id} alt={producto.nombre} />
        <span className="tienda-tarjeta-nombre" title={producto.nombre}>
          {producto.nombre}
        </span>
        <span className="tienda-tarjeta-marca">{producto.marca || "Sin marca"}</span>
        <strong className="tienda-precio">{precio(producto.precio)}</strong>
      </button>
      {children}
    </li>
  );
}

export default function Catalogo({
  abrir,
  avisarEjemplo,
}: {
  abrir: (id: number) => void;
  avisarEjemplo: (ejemplo: boolean) => void;
}) {
  const [texto, setTexto] = useState("");
  const [busqueda, setBusqueda] = useState({ q: "", tipo: "", pagina: 1 });
  const [datos, setDatos] = useState<CatalogoDatos>();
  const [cargando, setCargando] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => {
    let vigente = true;
    setCargando(true);
    setError("");
    buscarProductos(busqueda.q, busqueda.tipo, busqueda.pagina)
      .then((respuesta) => {
        if (!vigente) return;
        setDatos(respuesta.datos);
        avisarEjemplo(respuesta.ejemplo);
      })
      .catch((e: Error) => vigente && setError(e.message))
      .finally(() => vigente && setCargando(false));
    return () => {
      vigente = false;
    };
  }, [busqueda]);

  const paginas = datos ? Math.max(1, Math.ceil(datos.total / datos.por_pagina)) : 1;
  const desde = datos ? (datos.pagina - 1) * datos.por_pagina + 1 : 0;

  return (
    <section className="panel" aria-labelledby="catalogo-titulo">
      <div className="panel-heading">
        <h2 id="catalogo-titulo">Catálogo</h2>
        {datos && (
          <span className="muted small" role="status">
            {datos.total.toLocaleString("es-PE")} productos
          </span>
        )}
      </div>
      <form
        className="tienda-buscador"
        role="search"
        onSubmit={(event) => {
          event.preventDefault();
          setBusqueda({ ...busqueda, q: texto.trim(), pagina: 1 });
        }}
      >
        <input
          type="search"
          aria-label="Buscar productos"
          placeholder="Buscar: bar stool, running shoes, table lamp…"
          value={texto}
          onChange={(event) => setTexto(event.target.value)}
        />
        <select
          aria-label="Filtrar por tipo"
          value={busqueda.tipo}
          onChange={(event) =>
            setBusqueda({ ...busqueda, tipo: event.target.value, pagina: 1 })
          }
        >
          <option value="">Todos los tipos</option>
          {datos?.tipos.map((item) => (
            <option key={item.tipo} value={item.tipo}>
              {item.tipo} ({item.productos})
            </option>
          ))}
        </select>
        <button className="primary" type="submit" disabled={cargando}>
          Buscar
        </button>
      </form>
      {error && (
        <p className="message error tienda-estado" role="alert">
          {error}
        </p>
      )}
      {cargando && !datos && (
        <p className="empty-small" role="status">
          Cargando productos…
        </p>
      )}
      {datos && !datos.productos.length && (
        <p className="empty-small">No hay productos para esta búsqueda.</p>
      )}
      {datos && (
        <>
          <ul className="tienda-grilla" aria-label="Productos" aria-busy={cargando}>
            {datos.productos.map((producto) => (
              <Tarjeta key={producto.id} producto={producto} abrir={abrir} />
            ))}
          </ul>
          <div className="pagination">
            <span className="muted small">
              {datos.total
                ? `${desde}–${Math.min(desde + datos.por_pagina - 1, datos.total)} de ${datos.total}`
                : ""}
            </span>
            <div>
              <button
                disabled={cargando || datos.pagina <= 1}
                onClick={() => setBusqueda({ ...busqueda, pagina: datos.pagina - 1 })}
              >
                Anterior
              </button>
              <span className="muted small">
                Página {datos.pagina} de {paginas}
              </span>
              <button
                disabled={cargando || datos.pagina >= paginas}
                onClick={() => setBusqueda({ ...busqueda, pagina: datos.pagina + 1 })}
              >
                Siguiente
              </button>
            </div>
          </div>
          <details className="tienda-sql">
            <summary>Consulta SQL</summary>
            <code>{datos.consulta}</code>
          </details>
        </>
      )}
    </section>
  );
}
