import { datasetRows } from "./sql";

export interface DemoStep {
  sql: string;
  /** Qué mirar en el resultado o en el plan. */
  note?: string;
  /** DROP de limpieza: si la tabla no existe, no es un error de la demo. */
  cleanup?: boolean;
  /** El paso demuestra una validación: el motor debe rechazarlo. */
  expectError?: boolean;
}

export interface DemoSection {
  title: string;
  description: string;
  steps: DemoStep[];
}

export const demoTables = ["org_heap", "org_seq", "org_bp", "organizaciones"];

/**
 * Guion de la demo con las tres organizaciones. Con 1 000 filas usa los valores
 * del guion original (500, 100–120, 1–400, 1001); con más filas escalan.
 */
export function demoSections(csv: string): DemoSection[] {
  const n = datasetRows(csv) || 1000;
  const file = `datos/${csv}`;
  const k = Math.floor(n / 2);
  const a = Math.floor(n / 10);
  const b = a + 20;
  const cut = Math.floor(n * 0.4);
  const fresh = n + 1;
  const row = (name: string) =>
    `(${fresh}, 'abc', '${name}', 'http://x', 'Peru', 'demo', 2024, 'Software', 10)`;
  return [
    {
      title: "Limpiar",
      description:
        "Borra las tablas de una demo anterior. Si alguna no existe, se omite.",
      steps: [
        ...demoTables.map((table) => ({
          sql: `DROP TABLE ${table};`,
          cleanup: true,
        })),
        { sql: "SHOW TABLES;", note: "Debe quedar sin tablas de la demo." },
      ],
    },
    {
      title: "Cargar la misma tabla en las tres organizaciones",
      description: `El mismo CSV (${n.toLocaleString("es-PE")} filas) en Heap, Secuencial y B+ agrupado. Compara el tiempo de carga y las páginas de cada archivo.`,
      steps: [
        { sql: `CREATE TABLE org_heap FROM FILE '${file}' USING HEAP;` },
        { sql: `CREATE TABLE org_seq  FROM FILE '${file}' USING SEQUENTIAL;` },
        { sql: `CREATE TABLE org_bp   FROM FILE '${file}' USING BPLUS;` },
        { sql: "SHOW TABLES;", note: "Registros y páginas de cada tabla." },
        { sql: "DESCRIBE org_heap;", note: "Columnas, tipos y clave primaria." },
      ],
    },
    {
      title: "Misma consulta, tres estructuras",
      description:
        "Mira las páginas leídas en cada plan: el heap recorre el archivo, el secuencial hace búsqueda binaria y el B+ baja por el árbol.",
      steps: [
        {
          sql: `SELECT Index, Name, Country FROM org_heap WHERE Index = ${k};`,
          note: "scan_completo",
        },
        {
          sql: `SELECT Index, Name, Country FROM org_seq  WHERE Index = ${k};`,
          note: "busqueda_por_clave (binaria)",
        },
        {
          sql: `SELECT Index, Name, Country FROM org_bp   WHERE Index = ${k};`,
          note: "busqueda_por_clave (árbol)",
        },
      ],
    },
    {
      title: "Rango",
      description:
        "El secuencial y el B+ agrupado resuelven el rango con páginas consecutivas; el heap tiene que leer todo.",
      steps: [
        { sql: `SELECT Index, Name FROM org_seq WHERE Index BETWEEN ${a} AND ${b};` },
        { sql: `SELECT Index, Name FROM org_bp  WHERE Index BETWEEN ${a} AND ${b};` },
        { sql: `SELECT Index, Name FROM org_heap WHERE Index BETWEEN ${a} AND ${b};` },
      ],
    },
    {
      title: "Índice secundario sobre Founded",
      description:
        "B+ no agrupado: la misma consulta antes y después de crear el índice. Compara scan_completo contra busqueda_por_indice.",
      steps: [
        {
          sql: "SELECT Index, Name, Founded FROM org_heap WHERE Founded = 2019;",
          note: "sin índice: scan_completo",
        },
        { sql: "CREATE INDEX idx_founded ON org_heap (Founded);" },
        {
          sql: "SELECT Index, Name, Founded FROM org_heap WHERE Founded = 2019;",
          note: "con índice: busqueda_por_indice",
        },
        {
          sql: "SELECT Index, Founded FROM org_heap WHERE Founded BETWEEN 2020 AND 2022 ORDER BY Founded DESC;",
          note: "rango_por_indice + ordenamiento",
        },
      ],
    },
    {
      title: "Agregados y ordenamiento",
      description:
        "GROUP BY con hashing externo y ORDER BY con k-way merge. El plan muestra particiones, runs y pasadas.",
      steps: [
        {
          sql: "SELECT Country, COUNT(*), AVG(Number_of_employees), MAX(Founded) FROM org_bp GROUP BY Country ORDER BY Country LIMIT 15;",
        },
        {
          sql: "SELECT Index, Name, Number_of_employees FROM org_seq WHERE Founded >= 2015 ORDER BY Number_of_employees DESC LIMIT 10;",
        },
      ],
    },
    {
      title: "Insertar, clave repetida, borrar",
      description: `Inserta la clave ${fresh.toLocaleString("es-PE")} (libre), repite la misma clave (debe fallar), y borra un rango en el secuencial.`,
      steps: [
        { sql: `INSERT INTO org_bp VALUES ${row("Nueva SAC")};` },
        {
          sql: `INSERT INTO org_bp VALUES ${row("Repetida")};`,
          note: "debe fallar: clave primaria repetida",
          expectError: true,
        },
        { sql: "SELECT Index, Name, Country FROM org_bp WHERE Country = 'Peru';" },
        {
          sql: `DELETE FROM org_seq WHERE Index BETWEEN 1 AND ${cut};`,
          note: "eliminación por rango",
        },
        { sql: "SELECT COUNT(*) FROM org_seq;" },
        { sql: "SHOW TABLES;", note: "Registros y estado del archivo tras el DELETE." },
      ],
    },
    {
      title: "Cierre: páginas leídas por estructura",
      description:
        "La misma búsqueda por clave en las tres tablas. Compara paginas_leidas en el plan.",
      steps: [
        {
          sql: `SELECT * FROM org_bp  WHERE Index = ${k};`,
          note: "bplus_agrupado: busqueda_por_clave, raíz + hoja",
        },
        {
          sql: `SELECT * FROM org_seq WHERE Index = ${k};`,
          note: "secuencial: busqueda_por_clave, log₂ páginas (binaria)",
        },
        {
          sql: `SELECT * FROM org_heap WHERE Index = ${k};`,
          note: "heap: scan_completo hasta encontrar la clave",
        },
      ],
    },
  ];
}

/** Guion completo como texto para el editor, con comentarios por sección. */
export function demoScript(csv: string) {
  return demoSections(csv)
    .map(
      (section, i) =>
        `-- ${i}. ${section.title}\n` +
        section.steps
          .map((step) => step.sql + (step.note ? `  -- ${step.note}` : ""))
          .join("\n"),
    )
    .join("\n\n");
}
