import { useEffect, useRef, useState } from "react";
import { Thumbnail } from "../Gallery";
import { buscarPorFoto, buscarProductos, precio } from "./api";
import type {
  Catalogo as CatalogoDatos,
  FotoResultado,
  ProductoResumen,
} from "./types";

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
  const [minimo, setMinimo] = useState("");
  const [maximo, setMaximo] = useState("");
  const [busqueda, setBusqueda] = useState({
    q: "",
    tipo: "",
    pagina: 1,
    precioMin: "",
    precioMax: "",
  });
  const [datos, setDatos] = useState<CatalogoDatos>();
  const [cargando, setCargando] = useState(false);
  const [error, setError] = useState("");
  const [foto, setFoto] = useState<{ vista: string; nombre: string; archivo: File }>();
  const [metodo, setMetodo] = useState("auto");
  const [parecidos, setParecidos] = useState<FotoResultado>();
  const [buscandoFoto, setBuscandoFoto] = useState(false);
  const [errorFoto, setErrorFoto] = useState("");
  const campoArchivo = useRef<HTMLInputElement>(null);

  async function subirFoto(archivo: File, como = metodo) {
    if (!foto || foto.archivo !== archivo) {
      if (foto) URL.revokeObjectURL(foto.vista);
      setFoto({ vista: URL.createObjectURL(archivo), nombre: archivo.name, archivo });
    }
    setParecidos(undefined);
    setErrorFoto("");
    setBuscandoFoto(true);
    try {
      setParecidos(await buscarPorFoto(archivo, como));
    } catch (e) {
      setErrorFoto((e as Error).message);
    } finally {
      setBuscandoFoto(false);
    }
  }

  function quitarFoto() {
    if (foto) URL.revokeObjectURL(foto.vista);
    setFoto(undefined);
    setParecidos(undefined);
    setErrorFoto("");
  }

  function elegida(event: React.ChangeEvent<HTMLInputElement>) {
    const archivo = event.target.files?.[0];
    event.target.value = ""; // permite elegir la misma foto otra vez
    if (archivo) void subirFoto(archivo);
  }

  useEffect(() => {
    let vigente = true;
    setCargando(true);
    setError("");
    buscarProductos(
      busqueda.q,
      busqueda.tipo,
      busqueda.pagina,
      busqueda.precioMin,
      busqueda.precioMax,
    )
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
          if (minimo && maximo && Number(minimo) > Number(maximo)) {
            setError("El precio mínimo no puede ser mayor que el máximo.");
            return;
          }
          setBusqueda({
            ...busqueda,
            q: texto.trim(),
            pagina: 1,
            precioMin: minimo,
            precioMax: maximo,
          });
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
        <div className="tienda-precios" role="group" aria-label="Rango de precio">
          <input
            type="number"
            min="0"
            step="any"
            inputMode="decimal"
            aria-label="Precio mínimo"
            placeholder="S/ mín."
            value={minimo}
            onChange={(event) => setMinimo(event.target.value)}
          />
          <span className="muted" aria-hidden="true">
            –
          </span>
          <input
            type="number"
            min="0"
            step="any"
            inputMode="decimal"
            aria-label="Precio máximo"
            placeholder="S/ máx."
            value={maximo}
            onChange={(event) => setMaximo(event.target.value)}
          />
        </div>
        <button className="primary" type="submit" disabled={cargando}>
          Buscar
        </button>
      </form>
      <div className="tienda-foto-barra">
        <input
          ref={campoArchivo}
          type="file"
          accept="image/*"
          hidden
          onChange={elegida}
        />
        <button onClick={() => campoArchivo.current?.click()} disabled={buscandoFoto}>
          Buscar por foto
        </button>
        <span className="muted small">
          Sube la foto de un producto y busca los más parecidos.
        </span>
      </div>
      {foto && (
        <div className="tienda-foto-resultado" aria-live="polite">
          <div className="tienda-foto-cabecera">
            <img src={foto.vista} alt={`Tu foto: ${foto.nombre}`} />
            <div>
              <strong>Productos parecidos a tu foto</strong>
              <label className="small">
                Método{" "}
                <select
                  value={metodo}
                  disabled={buscandoFoto}
                  onChange={(event) => {
                    setMetodo(event.target.value);
                    void subirFoto(foto.archivo, event.target.value);
                  }}
                >
                  <option value="auto">Automático</option>
                  <option value="clip">CLIP (entiende el objeto)</option>
                  <option value="sift">SIFT + HNSW (motor)</option>
                </select>
              </label>
              {parecidos && (
                <span className="muted small"> · se buscó con {parecidos.metodo.toUpperCase()}</span>
              )}
              {buscandoFoto && <p className="muted small">Analizando la foto…</p>}
              {parecidos && !parecidos.productos.length && (
                <p className="muted small">No se encontraron productos parecidos.</p>
              )}
              <button className="text-button" onClick={quitarFoto}>
                Quitar la foto
              </button>
            </div>
          </div>
          {errorFoto && (
            <p className="message error tienda-estado" role="alert">
              {errorFoto}
            </p>
          )}
          {parecidos && (
            <>
              <ul className="tienda-grilla" aria-label="Productos parecidos">
                {parecidos.productos.map((producto) => (
                  <Tarjeta key={producto.id} producto={producto} abrir={abrir}>
                    <span className="muted small">
                      {Math.round(producto.similitud * 100)} % parecido
                    </span>
                  </Tarjeta>
                ))}
              </ul>
              <details className="tienda-sql">
                <summary>Consulta SQL</summary>
                <code>{parecidos.consulta}</code>
              </details>
            </>
          )}
        </div>
      )}
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
