# Interfaz React del minigestor BD2

Cliente local del motor C++ (vía FastAPI) con los cuatro paneles: archivos, consulta SQL, resultados y plan de ejecución. La pestaña **Demo guiada** recorre el guion de la exposición paso por paso con 1k, 10k o 100k registros. La pestaña **Comparar estructuras** carga el mismo CSV en Heap, Secuencial, B+ agrupado y B+ no agrupado, ejecuta las mismas consultas en las cuatro y compara páginas leídas, tiempo y resultados.

Desde la raíz del repo, un solo comando:

```bash
motor run
```

Instala lo que falte, compila el motor y abre http://127.0.0.1:5173. La primera
vez tarda cerca de un minuto. Para las pruebas, `motor test`.

En Linux y macOS, la primerísima vez hay que lanzarlo como `./bd2 run`: eso deja
instalado el comando `motor`.

Si prefieres hacerlo a mano: crea el `.venv` en la raíz, instala
`api/requirements.txt`, y desde `web/` lanza `npm install && npm run dev`.

Consulta el [instructivo completo](INSTRUCTIVO.md) y la [revisión de requisitos](../docs/revision_entrega_1.md).

La pestaña **Mediciones** genera gráficas desde CSV y JSON de los harnesses. Permite filtrar operación, tamaño, métrica y estructuras, y alternar a escala logarítmica; las series no están atadas a nombres concretos de índices.
