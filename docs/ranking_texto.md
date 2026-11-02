# Ranking de texto

`buscar` ([ranking.h](../motor/texto/ranking.h)) solo recorre los postings de los términos de la
consulta, acumula el score por documento y se queda con los `k` mejores usando un heap de
tamaño `k`.

## TF-IDF con coseno

```
tf_w = 1 + log10(tf)      (0 si tf = 0)
idf  = log10(N / df)
score = Σ tf_w(doc) · tf_w(consulta) · idf  /  (‖doc‖ · ‖consulta‖)
```

El documento usa solo `tf_w` y la consulta usa `tf_w · idf`, como en el ejemplo de la clase
(da 0,80). Las normas de los documentos se calculan al construir el índice.

## BM25

```
idf   = ln((N − df + 0,5) / (df + 0,5) + 1)
score = Σ idf · tf · (k1 + 1) / (tf + k1 · (1 − b + b · largo_doc / largo_promedio))
```

Con `k1 = 1,2` y `b = 0,75`, configurables. Fórmula de Robertson y Spärck Jones.

## Ejemplo

Documentos 0 = "cat dog", 1 = "cat cat bird", 2 = "dog bird fish" y consulta "cat fish".
N = 3, df(cat) = 2, df(fish) = 1 y largo promedio = 8/3.

Para el documento 2 con TF-IDF, el peso de la consulta es 0,176091 para `cat` y 0,477121 para
`fish`, su norma es 0,508579 y la del documento es 1,732051:
`0,477121 / (1,732051 · 0,508579) = 0,541639`.

Para el documento 2 con BM25 solo cuenta `fish`, con idf = 0,980829:
`0,980829 · 2,2 / (1 + 1,2 · (0,25 + 0,75 · 3 / 2,666667)) = 0,933113`.

| doc | TF-IDF | BM25 |
|---|---|---|
| 2 | 0,541639 | 0,933113 |
| 1 | 0,274520 | 0,624307 |
| 0 | 0,244830 | 0,523548 |

