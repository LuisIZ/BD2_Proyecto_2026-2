import { test, expect, type Page } from "@playwright/test";
import type { Result } from "../src/types";

// Respuestas de ejemplo del motor para las Partes 3 y 4: así la interfaz se
// prueba aunque MATCH (#55) y SIMILAR_TO (#62) todavía no estén en el motor.
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
