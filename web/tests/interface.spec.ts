import { test, expect } from "@playwright/test";

test("CSV, consultas reales, plan, paginación, errores y pantalla móvil", async ({
  page,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await expect(page.getByRole("status").first()).toContainText("tablas");
  await page.getByRole("button", { name: "Cargar CSV", exact: true }).click();
  await page.getByRole("button", { name: "Cargar tabla", exact: true }).click();
  await expect(page.getByRole("dialog")).not.toBeVisible({ timeout: 45000 });
  await expect(
    page.getByRole("button", { name: /organizaciones.*heap/ }),
  ).toBeVisible();
  const editor = page.getByRole("textbox", { name: "Editor SQL" });
  async function execute(sql: string) {
    await editor.fill(sql);
    await page.getByRole("button", { name: "Ejecutar", exact: true }).click();
    await expect(
      page.getByRole("button", { name: "Ejecutar", exact: true }),
    ).toBeEnabled();
  }
  await execute("SELECT Index, Name FROM organizaciones LIMIT 120;");
  await expect(
    page.getByRole("cell", { name: "Acevedo LLC", exact: true }),
  ).toBeVisible();
  await expect(
    page.getByText("1–50 de 120 filas", { exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Siguiente", exact: true }).click();
  await expect(
    page.getByText("51–100 de 120 filas", { exact: true }),
  ).toBeVisible();
  await expect(
    page.getByText("Lectura completa", { exact: true }),
  ).toBeVisible();
  const download = page.waitForEvent("download");
  await page.getByRole("button", { name: "Exportar CSV", exact: true }).click();
  expect((await download).suggestedFilename()).toBe("resultados.csv");
  await execute(
    "SELECT Country, COUNT(*) FROM organizaciones GROUP BY Country ORDER BY Country LIMIT 10;",
  );
  await expect(page.getByText("Agrupación", { exact: true })).toBeVisible();
  await expect(page.getByText("Ordenamiento", { exact: true })).toBeVisible();
  await execute(
    "CREATE INDEX idx_test ON organizaciones (Index); SELECT Index, Name FROM organizaciones WHERE Index = 5;",
  );
  await expect(
    page.getByText("Búsqueda por índice", { exact: true }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Sentencia 1", exact: true }),
  ).toBeVisible();
  await execute(
    "INSERT INTO organizaciones VALUES (100001, 'x', 'x', 'x', 'x', 'x', 2026, 'x', 1); SELECT Index FROM organizaciones WHERE Index = 100001;",
  );
  await expect(
    page.getByRole("cell", { name: "100001", exact: true }),
  ).toBeVisible();
  await execute(
    "DELETE FROM organizaciones WHERE Index = 100001; SELECT Index FROM organizaciones WHERE Index = 100001;",
  );
  await expect(
    page.getByText("La consulta no devolvió filas.", { exact: true }),
  ).toBeVisible();
  await execute("SELECT columna_inexistente FROM organizaciones;");
  await expect(
    page.getByText("No se ejecutó la sentencia", { exact: true }),
  ).toBeVisible();
  await execute(
    "SELECT Index, Name, Country, Founded FROM organizaciones WHERE Index BETWEEN 10 AND 20;",
  );
  await page.screenshot({
    path: "test-results/escritorio.png",
    fullPage: true,
  });
  await page.reload();
  await expect(
    page.getByRole("button", { name: /organizaciones.*heap/ }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Mediciones", exact: true }).click();
  // se elige el archivo a mano: cuál queda por defecto depende de qué benchmarks
  // se hayan corrido antes, y la lista va en orden alfabético
  await page
    .getByLabel("Archivo", { exact: true })
    .selectOption("heap_bench.csv");
  await expect(page.getByLabel(/Gráfica de/)).toBeVisible();
  await page.getByRole("button", { name: "Sintaxis", exact: true }).click();
  await expect(page.getByRole("heading", { name: "Sentencias" })).toBeVisible();
  await page.getByRole("button", { name: "Consultas", exact: true }).click();
  await execute("SELECT Index, Name FROM organizaciones LIMIT 10;");
  await page.setViewportSize({ width: 390, height: 844 });
  await page.screenshot({ path: "test-results/movil.png", fullPage: true });
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= window.innerWidth,
    ),
  ).toBe(true);
  expect(errors).toEqual([]);
});

test("carga local, organizaciones y validación de la API", async ({
  request,
}) => {
  for (const organizacion of ["SEQUENTIAL", "BPLUS"]) {
    const nombre = `prueba_${organizacion.toLowerCase()}`;
    const response = await request.post("/api/importar", {
      data: { nombre, organizacion, contenido: "id,nombre\n1,Ana\n2,Luis\n" },
    });
    expect(response.ok()).toBe(true);
    expect((await response.json()).resultados[0].ok).toBe(true);
    const query = await request.post("/api/consultas", {
      data: { sql: `SELECT * FROM ${nombre} WHERE id BETWEEN 1 AND 2;` },
    });
    const result = (await query.json()).resultados[0];
    expect(result.ok).toBe(true);
    expect(result.filas).toEqual([
      [1, "Ana"],
      [2, "Luis"],
    ]);
  }
  const invalid = await request.post("/api/importar", {
    data: { nombre: "invalid; DROP TABLE t", dataset: "../secret.csv" },
  });
  expect(invalid.status()).toBe(422);
  const traversal = await request.post("/api/importar", {
    data: { nombre: "test", dataset: "../secret.csv" },
  });
  expect(traversal.status()).toBe(400);
  const empty = await request.post("/api/consultas", { data: { sql: "   " } });
  expect(empty.status()).toBe(400);
});

test("el mapa carga puntos y resalta resultados espaciales y filas", async ({
  page,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("console", (message) => {
    if (message.type() === "error") errors.push(message.text());
  });
  await page.route("**/tile.openstreetmap.org/**", (route) =>
    route.fulfill({ status: 200, contentType: "image/png", body: Buffer.from([]) }),
  );
  await page.route("**/api/catalogo", (route) =>
    route.fulfill({
      contentType: "application/json",
      body: JSON.stringify({ tablas: [] }),
    }),
  );
  await page.goto("/");
  await expect(page.getByRole("status").first()).toContainText("tabla");
  await expect(
    page.getByText(
      "Selecciona una tabla con una columna POINT para mostrar sus puntos.",
    ),
  ).toBeVisible();
  await page.unroute("**/api/catalogo");

  const tableName = `mapa_test_${Date.now()}`;
  const editor = page.getByRole("textbox", { name: "Editor SQL" });
  await editor.fill(
    `CREATE TABLE ${tableName} (id INT PRIMARY KEY, nombre VARCHAR(20), ubicacion POINT) USING HEAP; ` +
      `INSERT INTO ${tableName} VALUES (1, 'Centro', POINT(-12.0464, -77.0428)); ` +
      `INSERT INTO ${tableName} VALUES (2, 'San Isidro', POINT(-12.0977, -77.0365)); ` +
      `INSERT INTO ${tableName} VALUES (3, 'Miraflores', POINT(-12.1211, -77.0297));`,
  );
  await page.getByRole("button", { name: "Ejecutar", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Ejecutar", exact: true }),
  ).toBeEnabled();
  const table = page.getByRole("button", {
    name: new RegExp(`${tableName}.*heap`, "i"),
  });
  await expect(table).toBeVisible();

  await editor.fill(
    `SELECT * FROM ${tableName} WHERE distancia(ubicacion, POINT(-12.0464, -77.0428)) <= 6000;`,
  );
  await page.getByRole("button", { name: "Ejecutar", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Ejecutar", exact: true }),
  ).toBeEnabled();
  await expect(
    page.getByRole("heading", { name: "Mapa espacial" }),
  ).toBeInViewport();
  await expect(page.getByText("3 puntos", { exact: true })).toBeVisible();
  await expect(page.locator(".spatial-map canvas")).toBeVisible();
  await expect(page.getByText("Radio: 6000 m", { exact: true })).toBeVisible();
  await expect(page.getByRole("cell", { name: "San Isidro" })).toBeVisible();
  await page.getByRole("row").filter({ hasText: "San Isidro" }).click();
  await expect(page.getByText("Fila seleccionada en el mapa")).toBeVisible();

  await editor.fill(
    `SELECT * FROM ${tableName} WHERE distancia(ubicacion, POINT(-12.5, -77.5)) < 1;`,
  );
  await page.getByRole("button", { name: "Ejecutar", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Ejecutar", exact: true }),
  ).toBeEnabled();
  await expect(page.getByText("Radio: 1 m", { exact: true })).toBeVisible();
  await expect(
    page.getByText("La consulta no devolvió filas para resaltar."),
  ).toBeVisible();

  await editor.fill(
    `SELECT * FROM ${tableName} ORDER BY distancia(ubicacion, POINT(-12.0464, -77.0428)) LIMIT 2;`,
  );
  await page.getByRole("button", { name: "Ejecutar", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Ejecutar", exact: true }),
  ).toBeEnabled();
  await expect(page.getByText("La consulta no devolvió filas para resaltar.")).toHaveCount(0);
  await expect(page.getByText("Fila seleccionada en el mapa")).toHaveCount(0);
  expect(errors).toEqual([]);
});

test("comparación de las cuatro estructuras", async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await expect(page.getByRole("status").first()).toContainText("tabla");
  await page
    .getByRole("button", { name: "Comparar estructuras", exact: true })
    .click();
  await page.getByLabel("CSV de origen").selectOption("organizations-1000.csv");
  await page.getByRole("button", { name: "Crear tablas", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Volver a crear", exact: true }),
  ).toBeVisible({ timeout: 60000 });
  await expect(page.locator(".task")).toHaveCount(0, { timeout: 60000 });
  await expect(page.getByText("principal=", { exact: false })).toBeVisible();
  const run = page.getByRole("button", {
    name: "Ejecutar en las cuatro",
    exact: true,
  });
  const expected: Record<string, string> = {
    "Búsqueda por clave": "Búsqueda por índice",
    "Rango por clave": "Rango por índice",
    "Columna no clave": "Búsqueda por índice",
    "ORDER BY": "Lectura completa",
    "GROUP BY": "Lectura completa",
    "INSERT y DELETE": "Búsqueda por índice",
  };
  for (const [name, access] of Object.entries(expected)) {
    await page.getByRole("button", { name, exact: true }).click();
    await run.click();
    await expect(run).toBeEnabled({ timeout: 60000 });
    await expect(page.locator(".check")).toHaveText(/^✓/);
    await expect(
      page.locator(".compare > section").nth(1).locator("tbody tr").nth(3),
    ).toContainText(access);
  }
  await page
    .getByRole("button", { name: "Medir con N creciente", exact: true })
    .click();
  await expect(page.locator(".findings")).toContainText(
    "COUNT(*) coincide en las cuatro tablas",
    { timeout: 60000 },
  );
  await expect(page.locator(".findings")).toContainText("deja de convenir");
  await page.locator(".hit-area").scrollIntoViewIfNeeded();
  const box = (await page.locator(".hit-area").boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await expect(page.locator(".chart-tooltip")).toBeVisible();
  await page.getByRole("button", { name: "Abrir en el editor" }).click();
  await expect(page.getByRole("textbox", { name: "Editor SQL" })).toHaveValue(
    /org_idx/,
  );
  await page.setViewportSize({ width: 390, height: 844 });
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= window.innerWidth,
    ),
  ).toBe(true);
  expect(errors).toEqual([]);
});

test("demo guiada paso a paso y sentencia bajo el cursor", async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await expect(page.getByRole("status").first()).toContainText("tabla");
  await page.getByRole("button", { name: "Demo guiada", exact: true }).click();
  await page
    .getByRole("group", { name: "Tamaño del dataset" })
    .getByRole("button", { name: "1k", exact: true })
    .click();
  const steps = page.locator(".demo-step");
  await expect(steps).toHaveCount(31);
  for (let i = 1; i <= 31; i++) {
    const next = page.getByRole("button", {
      name: `Ejecutar paso ${i}`,
      exact: true,
    });
    await next.click();
    await expect(
      page.getByRole("button", {
        name: i < 31 ? `Ejecutar paso ${i + 1}` : "Demo completa",
        exact: true,
      }),
    ).toBeVisible({ timeout: 60000 });
  }
  await expect(page.locator(".demo-progress")).toContainText("31 de 31");
  await expect(steps.nth(10)).toContainText("Lectura completa");
  await expect(steps.nth(11)).toContainText("Búsqueda por clave");
  await expect(steps.nth(12)).toContainText("Búsqueda por clave");
  await expect(steps.nth(17)).toContainText("creado sobre org_heap.Founded");
  await expect(steps.nth(18)).toContainText("Búsqueda por índice");
  await expect(steps.nth(23)).toContainText("rechazado como se esperaba");
  await expect(steps.nth(25)).toContainText("400 filas eliminadas");
  await expect(steps.locator(".demo-result.error")).toHaveCount(0);
  await expect(page.getByRole("heading", { name: "Plan de ejecución" })).toBeVisible();
  await page.getByRole("button", { name: "Ver resultado del paso 21" }).click();
  await expect(page.getByText("Agrupación", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Abrir guion en el editor" }).click();
  const editor = page.getByRole("textbox", { name: "Editor SQL" });
  await expect(editor).toHaveValue(/-- 1\. Cargar la misma tabla/);
  await editor.fill(
    "-- comentario\nSELECT COUNT(*) FROM org_bp; -- todo\nSELECT Index FROM org_bp WHERE Index = 7;",
  );
  await editor.evaluate((element: HTMLTextAreaElement) => {
    element.setSelectionRange(element.value.length - 3, element.value.length - 3);
  });
  await page
    .getByRole("button", { name: "Ejecutar sentencia", exact: true })
    .click();
  await expect(page.getByRole("cell", { name: "7", exact: true })).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Sentencia 1", exact: true }),
  ).toHaveCount(0);
  await page.getByRole("button", { name: "Ejecutar", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Sentencia 2", exact: true }),
  ).toBeVisible();
  expect(errors).toEqual([]);
});

test("EXPLAIN, grafo del plan y elección de clave e índices al cargar", async ({
  page,
}) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Cargar CSV" }).click();
  // las columnas se leen antes de cargar, para poder elegir clave e índices
  const key = page.getByLabel("Clave primaria");
  await expect(key).toHaveValue("Index");
  await page.getByLabel("Founded", { exact: true }).check();
  await page.getByLabel("Nombre de la tabla").fill("explain_demo");
  await page.getByRole("button", { name: "Cargar tabla" }).click();
  await expect(
    page.getByRole("button", { name: /explain_demo.*heap/ }),
  ).toBeVisible();

  // EXPLAIN describe sin ejecutar: el plan trae estimaciones pero no filas reales
  const editor = page.getByRole("textbox", { name: "Editor SQL" });
  await editor.fill("SELECT Name FROM explain_demo WHERE Founded = 2005;");
  await page.getByRole("button", { name: "EXPLAIN", exact: true }).click();
  await expect(page.getByText("plan estimado", { exact: false })).toBeVisible();
  await expect(
    page.getByRole("cell", { name: /Index Scan using/ }),
  ).toBeVisible();
  // el plan dice sobre qué columna se aplica el índice
  await expect(
    page.getByRole("cell", { name: /Index Cond: Founded = 2005/ }),
  ).toBeVisible();

  await page.getByRole("button", { name: "EXPLAIN ANALYZE" }).click();
  await expect(
    page.getByText("plan con medidas reales", { exact: false }),
  ).toBeVisible();
  const plan = page.locator(".plan");
  await expect(plan.getByText(/actual/).first()).toBeVisible();

  // el mismo plan, dibujado como grafo de nodos
  await plan.getByRole("button", { name: "Grafo" }).click();
  await expect(page.locator(".plan-node").first()).toBeVisible();
  await page.locator(".plan-node").first().hover();
  await expect(page.locator(".plan-tip")).toBeVisible();
});
