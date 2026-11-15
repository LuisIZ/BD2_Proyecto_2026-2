import { test, expect, type Page } from "@playwright/test";
import ejemplo from "../src/copyright/ejemplo.json" with { type: "json" };

/** Un wav de silencio de 45 s (8 kHz, 8 bits, mono), para que <audio> pueda buscar. */
function wav(segundos = 45, tasa = 8000) {
  const datos = segundos * tasa;
  const b = Buffer.alloc(44 + datos, 128);
  b.write("RIFF", 0);
  b.writeUInt32LE(36 + datos, 4);
  b.write("WAVEfmt ", 8);
  b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20);
  b.writeUInt16LE(1, 22);
  b.writeUInt32LE(tasa, 24);
  b.writeUInt32LE(tasa, 28);
  b.writeUInt16LE(1, 32);
  b.writeUInt16LE(8, 34);
  b.write("data", 36);
  b.writeUInt32LE(datos, 40);
  return b;
}

async function subir(page: Page, nombre = "remix.wav", buffer = wav()) {
  await page.goto("/");
  await page.getByRole("button", { name: "Copyright", exact: true }).click();
  await page.getByLabel("Audio a analizar").setInputFiles({ name: nombre, mimeType: "audio/wav", buffer });
}

test("sube un audio y muestra el veredicto, el timeline y las canciones", async ({ page }) => {
  const errores: string[] = [];
  page.on("pageerror", (error) => errores.push(error.message));
  let pedido = "";
  await page.route("**/api/copyright/analizar*", (route) => {
    pedido = route.request().url();
    return route.fulfill({ json: ejemplo });
  });
  await subir(page);
  expect(pedido).toContain("nombre=remix.wav");

  await expect(page.getByRole("region", { name: "Veredicto" })).toContainText("Posible uso no autorizado");
  await expect(page.getByRole("region", { name: "Veredicto" })).toContainText("74 %");

  // 8 tramos de 10 s cada 5 s; los 4 primeros son de pop.00011
  const tramos = page.locator(".timeline-tramo");
  await expect(tramos).toHaveCount(8);
  await expect(tramos.first()).toHaveAttribute("aria-label", /pop\.00011, 93\.8 %/);
  await expect(tramos.nth(4)).toHaveAttribute("aria-label", /sin coincidencias/);
  const color = (i: number) => tramos.nth(i).evaluate((e) => getComputedStyle(e).backgroundColor);
  expect(await color(0)).toBe(await color(3));
  expect(await color(0)).not.toBe(await color(6));
  // más similitud, más intensidad
  const opacidad = (i: number) => tramos.nth(i).evaluate((e) => Number(getComputedStyle(e).opacity));
  expect(await opacidad(0)).toBeGreaterThan(await opacidad(3));

  // pasar el mouse muestra la coincidencia; un clic reproduce desde ese segundo
  await tramos.nth(1).hover();
  await expect(page.locator(".timeline-detalle")).toContainText("pop.00011 (pop), del 0:10 al 0:20 del original · 91.2 %");
  await tramos.nth(6).click();
  await expect
    .poll(() => page.locator(".copyright-audio audio").evaluate((a: HTMLAudioElement) => Math.round(a.currentTime)))
    .toBeGreaterThanOrEqual(30);

  // canciones detectadas y el original
  const canciones = page.getByRole("list", { name: "Canciones detectadas" });
  await expect(canciones.getByRole("listitem")).toHaveCount(2);
  await expect(canciones.getByRole("listitem").first()).toContainText("pop.00011");
  await expect(canciones.getByRole("listitem").first()).toContainText("50 % de los tramos");
  await expect(canciones.getByRole("listitem").first()).toContainText("similitud máxima 93.8 %");
  // una consulta por tramo, con el vector recortado
  await page.getByText("Consultas SQL").click();
  await expect(page.getByText("Una por cada tramo con sonido (8)")).toBeVisible();
  await expect(page.locator(".copyright-sql code").first()).toContainText("VECTOR(0.081200001,0,0.1423, ...)");
  await canciones.getByRole("button", { name: "Escuchar original" }).first().click();
  await expect(page.getByLabel("Original pop.00011")).toHaveAttribute("src", "/api/audios/711");
  await page.screenshot({ path: "test-results/copyright.png", fullPage: true });
  expect(errores).toEqual([]);
});

test("una canción sin coincidencias", async ({ page }) => {
  await page.route("**/api/copyright/analizar*", (route) =>
    route.fulfill({
      json: {
        ...ejemplo,
        veredicto: "sin_coincidencias",
        canciones: [],
        tramos: ejemplo.tramos.map((t, i) => ({
          ...t,
          // el último tramo es silencio: la API lo trae sin coincidencias
          coincidencias: i === 7 ? [] : t.coincidencias.map((c) => ({ ...c, similitud: 60, supera_umbral: false })),
        })),
      },
    }),
  );
  await subir(page);
  await expect(page.getByRole("region", { name: "Veredicto" })).toContainText("Sin coincidencias");
  await expect(page.getByText("Ningún tramo supera el umbral.")).toBeVisible();
  await expect(page.locator(".timeline-tramo")).toHaveCount(8);
  await expect(page.locator('.timeline-tramo[aria-label*="sin coincidencias"]')).toHaveCount(8);
  await page.locator(".timeline-tramo").nth(7).hover();
  await expect(page.locator(".timeline-detalle")).toContainText("silencio, sin coincidencias");
});

test("los errores de la API y un archivo que no es audio se muestran", async ({ page }) => {
  await page.route("**/api/copyright/analizar*", (route) =>
    route.fulfill({
      status: 400,
      json: { detail: "no existe la tabla segmentos. Corre docs/copyright.sql antes de analizar." },
    }),
  );
  await subir(page);
  await expect(page.getByRole("alert")).toContainText("Corre docs/copyright.sql antes de analizar.");
  await expect(page.getByRole("region", { name: "Veredicto" })).toHaveCount(0);

  await page.getByLabel("Audio a analizar").setInputFiles({ name: "notas.txt", mimeType: "text/plain", buffer: Buffer.from("hola") });
  await expect(page.getByRole("alert")).toContainText("Sube un audio wav, mp3, flac u ogg.");
});
