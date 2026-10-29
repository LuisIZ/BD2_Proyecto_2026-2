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
