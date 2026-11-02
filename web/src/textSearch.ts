/**
 * Búsqueda de texto (Parte 3): reconoce `MATCH(col, 'consulta')` en el SQL para
 * mostrar la columna de score y resaltar los términos de la consulta.
 */
export interface TextSearch {
  column: string;
  query: string;
  ranking?: string;
  /** palabras de la consulta sin stopwords, para mostrarlas */
  terms: string[];
  /** términos reducidos a su raíz, para resaltar también "cars" con "car" */
  stems: string[];
  /** alias de SCORE() en el SELECT, o "score" si no tiene */
  scoreColumn: string;
}

// las mismas stopwords básicas que quita el preprocesamiento (inglés, como ABO)
const stopwords = new Set(
  "a an and are as at be but by for from has have in is it its of on or that the this to was were will with".split(
    " ",
  ),
);

/** Raíz aproximada para resaltar; el motor usa Porter, aquí basta quitar sufijos comunes. */
export function stem(word: string) {
  let w = word.toLowerCase();
  // plurales como el paso 1a de Porter: sses → ss, ies → y, s → (nada), ss se queda
  if (w.endsWith("sses")) w = w.slice(0, -2);
  else if (w.endsWith("ies") && w.length > 4) w = `${w.slice(0, -3)}y`;
  else if (w.endsWith("s") && !w.endsWith("ss") && w.length > 3) w = w.slice(0, -1);
  for (const suffix of ["ing", "ed", "ly"]) {
    if (w.length - suffix.length >= 3 && w.endsWith(suffix))
      return w.slice(0, -suffix.length);
  }
  return w;
}

export function parseTextSearch(sql: string): TextSearch | undefined {
  const match = /\bMATCH\s*\(\s*([\w.]+)\s*,\s*'((?:''|[^'])*)'\s*\)/i.exec(sql);
  if (!match) return;
  const query = match[2].replaceAll("''", "'");
  const alias = /\bSCORE\s*\(\s*\)\s+(?:AS\s+)?(\w+)/i.exec(sql);
  const ranking = /\bUSING\s+(TF_IDF|BM25)\b/i.exec(sql);
  const terms = [
    ...new Set(
      (query.toLowerCase().match(/[\p{L}\p{N}]+/gu) ?? []).filter(
        (word) => !stopwords.has(word),
      ),
    ),
  ];
  return {
    column: match[1],
    query,
    ranking: ranking?.[1].toUpperCase(),
    terms,
    stems: [...new Set(terms.map(stem))],
    scoreColumn: alias?.[1] ?? "score",
  };
}

export interface Fragment {
  text: string;
  hit: boolean;
}

/** Parte el texto en fragmentos, marcando las palabras cuya raíz está en la consulta. */
export function highlight(text: string, stems: string[]): Fragment[] {
  if (!stems.length) return [{ text, hit: false }];
  const fragments: Fragment[] = [];
  let last = 0;
  for (const word of text.matchAll(/[\p{L}\p{N}]+/gu)) {
    const start = word.index ?? 0;
    if (!stems.includes(stem(word[0]))) continue;
    if (start > last) fragments.push({ text: text.slice(last, start), hit: false });
    fragments.push({ text: word[0], hit: true });
    last = start + word[0].length;
  }
  if (last < text.length) fragments.push({ text: text.slice(last), hit: false });
  return fragments;
}
