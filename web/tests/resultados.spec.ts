import { test, expect, type Page } from "@playwright/test";
import type { Result } from "../src/types";

// Respuestas de ejemplo del motor para las Partes 3 y 4: así la interfaz se
// prueba sin levantar la API ni cargar los datos.
async function answer(page: Page, result: Result) {
  await page.route("**/api/consultas", (route) =>
    route.fulfill({ json: { resultados: [result] } }),
  );
}

async function execute(page: Page, sql: string) {
  await page.getByRole("textbox", { name: "Editor SQL" }).fill(sql);
  await page.getByRole("button", { name: "Ejecutar", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Ejecutar", exact: true }),
  ).toBeEnabled();
}

test("MATCH muestra el score y resalta los términos", async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await answer(page, {
    ok: true,
    tipo: "select",
    mensaje: "3 filas",
    tiempo_ms: 1.2,
    columnas: ["id", "nombre", "relevancia"],
    filas: [
      [7, "Running Shoes for Men", 3.2104],
      [2, "Trail runner shoe", 1.6052],
      [9, "Wooden shoe rack", 0.4],
    ],
    plan: [],
  });
  await execute(
    page,
    "SELECT id, nombre, SCORE() AS relevancia FROM productos WHERE MATCH(descripcion, 'running shoes') USING BM25 ORDER BY relevancia DESC LIMIT 10;",
  );
  await expect(page.getByRole("note")).toContainText("con BM25");
  await expect(page.getByRole("note")).toContainText("«running shoes»");
  await expect(page.getByRole("note")).toContainText(
    "términos resaltados: running, shoes",
  );
  await expect(page.getByRole("columnheader", { name: "relevancia" })).toHaveClass(
    /score-col/,
  );
  await expect(page.locator("td.score-cell").first()).toHaveText("3.2104");
  // "Shoes" y "shoe" tienen la misma raíz que "shoes"; "Wooden" no se resalta
  await expect(page.locator("td mark")).toHaveText([
    "Running",
    "Shoes",
    "shoe",
    "shoe",
  ]);
  expect(errors).toEqual([]);
});

test("la tabla de productos muestra la foto junto al id", async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await answer(page, {
    ok: true,
    tipo: "select",
    mensaje: "2 filas",
    tiempo_ms: 1.1,
    columnas: ["id", "nombre", "relevancia"],
    filas: [
      [4125, "Rivet Modern Metal Dome-Shaped Table Lamp", 14.7984],
      [5466, "Rivet Contemporary Table Desk Lamp", 14.7984],
    ],
    plan: [],
  });
  await execute(
    page,
    "SELECT id, nombre, SCORE() AS relevancia FROM productos WHERE MATCH(descripcion, 'table lamp') USING BM25 ORDER BY relevancia DESC LIMIT 2;",
  );
  const images = page.getByRole("button", { name: "Imágenes" });
  await expect(images).toHaveAttribute("aria-pressed", "true");
  // la foto la sirve /api/imagenes/{id}; sin el dataset muestra "sin imagen"
  const first = page.locator("td.id-image").first();
  await expect(first).toContainText("4125");
  await expect(
    first.locator('img[src="/api/imagenes/4125"], [aria-label^="Sin imagen"]'),
  ).toBeVisible();
  await images.click();
  await expect(images).toHaveAttribute("aria-pressed", "false");
  await expect(page.locator("td.id-image")).toHaveCount(0);
  await expect(page.getByRole("cell", { name: "4125", exact: true })).toBeVisible();
  expect(errors).toEqual([]);
});

test("una tabla que no es de productos no muestra fotos hasta pedirlas", async ({ page }) => {
  await page.goto("/");
  await answer(page, {
    ok: true,
    tipo: "select",
    mensaje: "1 fila",
    tiempo_ms: 0.4,
    columnas: ["id", "nombre"],
    filas: [[1, "Barranco"]],
    plan: [],
  });
  await execute(page, "SELECT id, nombre FROM tiendas;");
  await expect(page.getByRole("button", { name: "Imágenes" })).toHaveAttribute(
    "aria-pressed",
    "false",
  );
  await expect(page.locator("td.id-image")).toHaveCount(0);
});

test("k-NN de vectores muestra la galería de miniaturas", async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  // mismas filas que devuelve el motor con los vectores SIFT de productos_1k
  await answer(page, {
    ok: true,
    tipo: "select",
    mensaje: "3 filas",
    tiempo_ms: 3.67,
    columnas: ["id", "nombre"],
    filas: [
      [1, "Rivet Wooden Door to an Old City Photo Print with Oak Hanger"],
      [290, "Stone & Beam Modern Family Audio Video Media Cabinets"],
      [994, "Movian TV Unit with Storage, 40 x 154cm"],
    ],
    plan: [
      { operacion: "proyeccion", nodo: "Projection", nivel: 0 },
      { operacion: "knn_vectorial", nodo: "Seq Vector Scan", nivel: 1 },
    ],
  });
  await execute(
    page,
    "SELECT id, nombre FROM galeria ORDER BY distancia(v, VECTOR(0.1, 0.2, 0.3), 'coseno') LIMIT 3;",
  );
  await expect(page.getByRole("note")).toContainText(
    "Búsqueda por similitud (k-NN secuencial), métrica coseno, k = 3",
  );
  const gallery = page.getByRole("list", { name: "Resultados por similitud" });
  await expect(gallery.locator(".gallery-rank")).toHaveText(["1", "2", "3"]);
  await expect(gallery.locator(".gallery-name").first()).toContainText(
    "Wooden Door",
  );
  // cada miniatura la sirve /api/imagenes/{id}; sin el dataset muestra "sin imagen"
  await expect(
    gallery.locator('img[src="/api/imagenes/290"], [aria-label^="Sin imagen"]').first(),
  ).toBeVisible();
  // espera a que cada miniatura termine: o carga la imagen o muestra "sin imagen"
  await expect
    .poll(() =>
      gallery
        .locator(".gallery-item")
        .evaluateAll((items) =>
          items.every((item) => {
            const img = item.querySelector("img");
            return !img || (img.complete && img.naturalWidth > 0);
          }),
        ),
    )
    .toBe(true);
  await page.screenshot({ path: "test-results/galeria.png" });
  await page.getByRole("button", { name: "Tabla", exact: true }).click();
  await expect(page.getByRole("cell", { name: "290", exact: true })).toBeVisible();
  expect(errors).toEqual([]);
});

test("MATCH real sobre productos_1k con Text Scan en el plan", async ({ page }) => {
  test.setTimeout(120000);
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await expect(page.getByRole("status").first()).toContainText("tabla");
  const tabla = `productos_${Date.now()}`;
  // el CSV de 1k está en el repo (docs/dataset.md), así que esta prueba usa el motor real
  await execute(
    page,
    `CREATE TABLE ${tabla} (id INT PRIMARY KEY, item_id VARCHAR(20), nombre VARCHAR(200), tipo VARCHAR(50), categoria VARCHAR(200), marca VARCHAR(80), color VARCHAR(40), descripcion VARCHAR(1000), imagen VARCHAR(80)); ` +
      `COPY ${tabla} FROM FILE 'datos/productos_1k.csv'; ` +
      `CREATE INDEX idx_${tabla} ON ${tabla} (descripcion) USING SPIMI;`,
  );
  await execute(
    page,
    `SELECT id, nombre, SCORE() AS relevancia FROM ${tabla} WHERE MATCH(descripcion, 'wooden door') USING BM25 ORDER BY relevancia DESC LIMIT 5;`,
  );
  await expect(page.getByRole("note")).toContainText("con BM25");
  await expect(page.locator("td.score-cell")).toHaveCount(5);
  await expect(page.locator("td mark").first()).toHaveText(/wooden|door/i);
  const textScan = page.locator(".plan-list strong").filter({ hasText: "Text Scan" });
  await expect(textScan).toBeVisible();
  await expect(textScan.locator(".plan-es")).toHaveText("Búsqueda de texto");
  await expect(page.getByText("Index Cond:", { exact: true })).toBeVisible();
  await page.screenshot({ path: "test-results/texto.png", fullPage: true });
  expect(errors).toEqual([]);
});

test("subir un archivo arma SIMILAR_TO y lo muestra con los resultados", async ({ page }) => {
  await page.goto("/");
  await page.route("**/api/multimedia/subir*", (route) =>
    route.fulfill({ json: { archivo: "0123456789abcdef.mp3", consulta: "" } }),
  );
  await page
    .getByLabel("Archivo de consulta")
    .setInputFiles({ name: "cancion.mp3", mimeType: "audio/mpeg", buffer: Buffer.from("x") });
  const editor = page.getByRole("textbox", { name: "Editor SQL" });
  await expect(editor).toHaveValue(/SIMILAR_TO\('0123456789abcdef\.mp3', k=10\)/);

  await answer(page, {
    ok: true,
    tipo: "select",
    mensaje: "1 fila",
    tiempo_ms: 1,
    columnas: ["id", "score"],
    filas: [[7, 0.98]],
    plan: [{ operacion: "knn_vectorial", nodo: "Seq Vector Scan", nivel: 0 }],
  });
  await execute(page, "SELECT id, SIMILARITY_SCORE() FROM canciones WHERE SIMILAR_TO('0123456789abcdef.mp3', k=1);");
  await expect(page.getByText("Archivo de consulta")).toBeVisible();
  await expect(page.locator('.gallery-query audio[src$="0123456789abcdef.mp3"]')).toBeVisible();
  await expect(page.getByRole("note")).toContainText("k = 1");
});

test("un archivo que la API rechaza muestra el error", async ({ page }) => {
  await page.goto("/");
  await page
    .getByLabel("Archivo de consulta")
    .setInputFiles({ name: "notas.txt", mimeType: "text/plain", buffer: Buffer.from("hola") });
  await expect(page.getByText(/extension no admitida/)).toBeVisible();
});

test("ejemplos y sintaxis de texto, espacial y vectores", async ({ page }) => {
  await page.goto("/");
  const ejemplos = page.getByLabel("Ejemplos SQL");
  const editor = page.getByRole("textbox", { name: "Editor SQL" });
  await ejemplos.selectOption("texto");
  await expect(editor).toHaveValue(/MATCH\(\w+, 'wooden door'\) USING BM25/);
  await ejemplos.selectOption("knnEspacial");
  await expect(editor).toHaveValue(/ORDER BY distancia\(\w+, POINT\(-12\.0464, -77\.0428\)\) LIMIT 5/);
  await ejemplos.selectOption("knnVectorial");
  await expect(editor).toHaveValue(/distancia\(\w+, VECTOR\((1, ){255}1\), 'coseno'\) LIMIT 10/);
  await page.getByRole("button", { name: "Sintaxis", exact: true }).click();
  await expect(page.getByText(/USING RTREE/)).toBeVisible();
  await expect(page.getByRole("cell", { name: "ORDER BY distancia(VECTOR) LIMIT k" })).toBeVisible();
});
