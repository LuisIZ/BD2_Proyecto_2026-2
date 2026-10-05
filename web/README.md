# Interfaz React del minigestor BD2

Cliente local del motor C++ (vía FastAPI) con cinco paneles: Archivos, Consultas, Resultados, Plan y Mapa interactivo. El mapa usa Leaflet para visualizar columnas `POINT` o coordenadas latitud/longitud, resaltar rango, k-NN y polígono, y sincronizar la selección de filas.

Desde la raíz del repo, un solo comando:

```bash
motor run
```

Instala lo que falte, compila el motor y abre http://127.0.0.1:5173. La primera
vez tarda cerca de un minuto. Para probar el mapa, pulsa **Cargar datos de prueba**
y sigue los pasos de aceptación de la sección correspondiente en el README de la raíz.

En Linux y macOS, la primerísima vez hay que lanzarlo como `./bd2 run`: eso deja
instalado el comando `motor`.

Si prefieres hacerlo a mano: crea el `.venv` en la raíz, instala
`api/requirements.txt`, y desde `web/` lanza `npm install && npm run dev`.

Consulta el [instructivo completo](INSTRUCTIVO.md) y la [revisión de requisitos](../docs/revision_entrega_1.md).
