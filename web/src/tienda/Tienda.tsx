import { useEffect, useState } from "react";
import { Thumbnail } from "../Gallery";
import Catalogo from "./Catalogo";
import Recomendaciones from "./Recomendaciones";
import SedesMapa from "./SedesMapa";
import { obtenerProducto, precio } from "./api";
import type { Producto } from "./types";

function Ficha({ id, volver }: { id: number; volver: () => void }) {
  const [producto, setProducto] = useState<Producto>();
  const [error, setError] = useState("");

  useEffect(() => {
    let vigente = true;
    setProducto(undefined);
    setError("");
    obtenerProducto(id)
      .then((respuesta) => vigente && setProducto(respuesta.datos))
      .catch((e: Error) => vigente && setError(e.message));
    return () => {
      vigente = false;
    };
  }, [id]);

  return (
    <section className="panel tienda-ficha" aria-labelledby="ficha-titulo">
      <div className="panel-heading">
        <button className="text-button" onClick={volver}>
          ← Volver al catálogo
        </button>
        <span className="muted small">producto {id}</span>
      </div>
      {error && (
        <p className="message error tienda-estado" role="alert">
          {error}
        </p>
      )}
      {!producto && !error && (
        <p className="empty-small" role="status">
          Cargando producto…
        </p>
      )}
      {producto && (
        <div className="tienda-ficha-cuerpo">
          <div className="tienda-ficha-foto">
            <Thumbnail id={producto.id} alt={producto.nombre} />
          </div>
          <div>
            <h2 id="ficha-titulo">{producto.nombre}</h2>
            <p className="tienda-precio grande">{precio(producto.precio)}</p>
            <dl className="tienda-datos">
              <dt>Marca</dt>
              <dd>{producto.marca || "—"}</dd>
              <dt>Color</dt>
              <dd>{producto.color || "—"}</dd>
              <dt>Tipo</dt>
              <dd>{producto.tipo}</dd>
              {producto.categoria && (
                <>
                  <dt>Categoría</dt>
                  <dd>{producto.categoria}</dd>
                </>
              )}
            </dl>
            <p className="tienda-descripcion">{producto.descripcion}</p>
          </div>
        </div>
      )}
    </section>
  );
}

/** Vista "Tienda" (#91): catálogo, ficha, recomendaciones y sedes cercanas con stock. */
export default function Tienda() {
  const [abierto, setAbierto] = useState<number>();
  const [ejemplo, setEjemplo] = useState(false);

  function abrir(id: number) {
    setAbierto(id);
    window.scrollTo({ top: 0, behavior: "smooth" });
  }

  return (
    <div className="tienda">
      {ejemplo && (
        <p className="panel-note warn" role="note">
          Datos de ejemplo: la API todavía no tiene los endpoints de la tienda (#90), así que se
          muestra web/src/tienda/ejemplo.json.
        </p>
      )}
      <div hidden={abierto !== undefined}>
        <Catalogo abrir={abrir} avisarEjemplo={setEjemplo} />
      </div>
      {abierto !== undefined && (
        <>
          <Ficha id={abierto} volver={() => setAbierto(undefined)} />
          <Recomendaciones id={abierto} abrir={abrir} />
          <SedesMapa id={abierto} />
        </>
      )}
    </div>
  );
}
