export interface Statement {
  text: string;
  start: number;
  end: number;
}

/** Quita comentarios `-- ...` hasta fin de línea, respetando literales entre comillas simples. */
export function stripComments(sql: string) {
  let out = "";
  let quoted = false;
  for (let i = 0; i < sql.length; i++) {
    const c = sql[i];
    if (quoted) {
      out += c;
      if (c === "'") quoted = false;
    } else if (c === "'") {
      quoted = true;
      out += c;
    } else if (c === "-" && sql[i + 1] === "-") {
      const end = sql.indexOf("\n", i);
      if (end === -1) break;
      i = end - 1;
    } else out += c;
  }
  return out;
}

/** Separa por `;` fuera de comillas y comentarios. Conserva las posiciones del texto original. */
export function splitStatements(sql: string): Statement[] {
  const statements: Statement[] = [];
  let quoted = false;
  let start = 0;
  const push = (end: number) => {
    const text = stripComments(sql.slice(start, end)).trim();
    if (text) statements.push({ text, start, end });
    start = end;
  };
  for (let i = 0; i < sql.length; i++) {
    const c = sql[i];
    if (quoted) {
      if (c === "'") quoted = false;
    } else if (c === "'") quoted = true;
    else if (c === "-" && sql[i + 1] === "-") {
      const end = sql.indexOf("\n", i);
      if (end === -1) break;
      i = end;
    } else if (c === ";") push(i + 1);
  }
  push(sql.length);
  return statements;
}

/** Sentencia que contiene al cursor; si está en un hueco, la anterior. */
export function statementAt(sql: string, cursor: number) {
  const statements = splitStatements(sql);
  return (
    statements.find((s) => cursor >= s.start && cursor <= s.end) ??
    [...statements].reverse().find((s) => s.start <= cursor) ??
    statements[0]
  );
}

/** Cantidad de filas de un CSV del proyecto según su nombre (organizations-10000.csv → 10000). */
export function datasetRows(name: string) {
  const match = /(\d+)\.csv$/i.exec(name);
  return match ? Number(match[1]) : 0;
}

export function datasetLabel(name: string) {
  const rows = datasetRows(name);
  if (!rows) return name;
  const short =
    rows % 1000 === 0 ? `${rows / 1000}k` : rows.toLocaleString("es-PE");
  return `${short} · ${name}`;
}
