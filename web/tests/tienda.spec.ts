import { test, expect, type Page } from "@playwright/test";
import ejemplo from "../src/tienda/ejemplo.json" with { type: "json" };

// Simula /api/tienda con las respuestas de ejemplo.json (el contrato de #90)
async function simularTienda(page: Page) {
  const pedidos: string[] = [];
  await page.route("**/api/tienda/**", (route) => {
    const url = new URL(route.request().url());
    pedidos.push(url.pathname + url.search);
    const ruta = url.pathname.replace("/api/tienda/", "");
    const json = ruta.endsWith("/recomendaciones")
      ? ejemplo.recomendaciones
      : ruta.endsWith("/sedes")
        ? {
            ...ejemplo.sedes,
            origen: {
              lat: Number(url.searchParams.get("lat")),
              lon: Number(url.searchParams.get("lon")),
            },
          }
        : /^productos\/\d+$/.test(ruta)
          ? ejemplo.producto
          : ejemplo.productos;
    return route.fulfill({ json });
  });
  return pedidos;
}

test("buscar, abrir una ficha, ver 10 recomendaciones y las sedes en el mapa", async ({ page }) => {
  const errores: string[] = [];
  page.on("pageerror", (error) => errores.push(error.message));
  const pedidos = await simularTienda(page);
  await page.goto("/");
  await page.getByRole("button", { name: "Tienda", exact: true }).click();

  // catálogo
  const productos = page.getByRole("list", { name: "Productos" });
  await expect(productos.getByRole("listitem")).toHaveCount(12);
  await page.getByRole("searchbox", { name: "Buscar productos" }).fill("bar stool");
  await page.getByRole("button", { name: "Buscar", exact: true }).click();
  await expect.poll(() => pedidos.some((p) => p.includes("q=bar+stool"))).toBe(true);
  await page.getByLabel("Filtrar por tipo").selectOption("STOOL_SEATING");
  await expect.poll(() => pedidos.some((p) => p.includes("tipo=STOOL_SEATING"))).toBe(true);
  await expect(page.getByText("1–12 de 27")).toBeVisible();
  await expect(page.getByText("Página 1 de 3")).toBeVisible();
  await page.getByRole("button", { name: "Siguiente", exact: true }).click();
  await expect.poll(() => pedidos.some((p) => p.includes("pagina=2"))).toBe(true);
  await expect(productos).toContainText("S/");

  // ficha
  await productos.getByRole("button").first().click();
  await expect(page.getByRole("heading", { name: ejemplo.producto.nombre })).toBeVisible();
  await expect(page.locator(".tienda-datos")).toContainText(ejemplo.producto.tipo);
  await expect(page.locator(".tienda-precio.grande")).toContainText("537.90");

  // recomendaciones
  const recomendaciones = page.getByRole("list", { name: "Recomendaciones" });
  await expect(recomendaciones.getByRole("listitem")).toHaveCount(10);
  const primera = recomendaciones.getByRole("listitem").first();
  await expect(primera.locator(".tienda-fuente")).toHaveText(["Texto", "Imagen", "Metadata"]);
  await primera.getByText("Por qué aparece").click();
  await expect(primera.locator(".tienda-puestos")).toContainText("USING HNSW");
  await expect(primera.locator(".tienda-puestos")).toContainText("puesto");

  // sedes cercanas con stock
  const sedes = page.getByRole("list", { name: "Sedes cercanas" });
  await expect(sedes.getByRole("listitem")).toHaveCount(3);
  await expect(sedes.getByRole("listitem").first()).toContainText("Tienda Santiago de Surco");
  await expect(sedes.getByRole("listitem").first()).toContainText("34 unidades");
  await expect(page.locator(".tienda-mapa .knn-label.leaflet-marker-icon")).toHaveCount(3);
  // un clic en el mapa cambia la ubicación y vuelve a pedir las sedes
  const antes = pedidos.filter((p) => p.includes("/sedes")).length;
  await page.locator(".tienda-mapa .leaflet-container").click({ position: { x: 80, y: 80 } });
  await expect
    .poll(() => pedidos.filter((p) => p.includes("/sedes")).length)
    .toBeGreaterThan(antes);
  await page.screenshot({ path: "test-results/tienda-ficha.png", fullPage: true });

  // volver al catálogo
  await page.getByRole("button", { name: "← Volver al catálogo" }).click();
  await expect(productos).toBeVisible();
  await page.screenshot({ path: "test-results/tienda-catalogo.png", fullPage: true });
  expect(errores).toEqual([]);
});

test("sin los endpoints de #90 muestra los datos de ejemplo con un aviso", async ({ page }) => {
  await page.route("**/api/tienda/**", (route) =>
    route.fulfill({ status: 404, json: { detail: "Not Found" } }),
  );
  await page.goto("/");
  await page.getByRole("button", { name: "Tienda", exact: true }).click();
  await expect(page.getByRole("note")).toContainText("Datos de ejemplo");
  await expect(page.getByRole("list", { name: "Productos" }).getByRole("listitem")).toHaveCount(12);
});

test("muestra el error de la API y el aviso de una fuente que falló", async ({ page }) => {
  await page.route("**/api/tienda/productos?*", (route) =>
    route.fulfill({ status: 500, json: { detail: "el motor no respondio" } }),
  );
  await page.goto("/");
  await page.getByRole("button", { name: "Tienda", exact: true }).click();
  await expect(page.getByRole("alert")).toContainText("el motor no respondio");

  await page.unrouteAll();
  await simularTienda(page);
  await page.route("**/api/tienda/productos/*/recomendaciones*", (route) =>
    route.fulfill({
      json: {
        ...ejemplo.recomendaciones,
        fuentes: ejemplo.recomendaciones.fuentes.map((f) =>
          f.fuente === "compras" ? { ...f, ok: false, error: "no hay compras", candidatos: 0 } : f,
        ),
      },
    }),
  );
  await page.getByRole("button", { name: "Buscar", exact: true }).click();
  await page.getByRole("list", { name: "Productos" }).getByRole("button").first().click();
  await expect(page.getByText("La fuente Compras falló: no hay compras")).toBeVisible();
  await expect(page.getByText("fusión RRF de 3 listas")).toBeVisible();
});
