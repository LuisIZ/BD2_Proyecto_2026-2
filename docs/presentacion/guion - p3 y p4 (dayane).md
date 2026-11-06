# Guion de la presentación: partes de Dayane

Cada bloque `[n]` es del archivo `demo - p3 y p4 (dayane).sql`. En la web se selecciona el
bloque y se pulsa **Ejecutar** (o `Shift + Enter` sobre una sentencia).

**Antes de empezar:** abrir la web con `npm run dev` y la API levantada. Si las tablas
`productos`, `fotos` o `tiendas` ya existen, ejecutar primero el bloque **[19]**.

Lo mío son cuatro cosas:
1. el dataset de productos;
2. la comparación de texto (TF-IDF, BM25 y GIN);
3. el índice IVF y la comparación de vectores;
4. cómo se ven los resultados en la web.

---

## 1. Dataset ABO (para las Partes 3 y 4)

**Qué digo:**
> Para las Partes 3 y 4 elegí el dataset **Amazon Berkeley Objects (ABO)**, de Amazon, con
> licencia libre CC BY 4.0. Lo elegimos porque cada producto tiene **texto y una foto**, y los
> dos comparten el mismo `id`. Así el texto sirve para la Parte 3 y la foto para la Parte 4.
>
> ABO trae 147 mil registros porque repite cada producto en cada país de Amazon. Con un script
> en Python me quedé con un registro por producto, solo en inglés, sin HTML y con la
> descripción recortada a 1 000 bytes para que cada fila quepa en una página de 4 KB.
> Quedaron 121 mil productos, y de ahí sacamos tres tamaños: 1k, 10k y 100k.

**Ahora lo comprobamos en la web.** Ejecuto los bloques **[1]** y **[2]**.

**Qué se ve:**
- `COUNT(*)` muestra **10000**.
- Cinco productos con su nombre, tipo, categoría e imagen. Por ejemplo, el 5 es un banco de bar
  (`STOOL_SEATING`); lo vamos a usar más adelante.

---

## 2. Parte 3: TF-IDF frente a BM25 (y GIN de PostgreSQL)

**Qué digo:**
> Sobre la descripción hay un índice invertido SPIMI. Cada palabra apunta a la lista de
> productos que la contienen, así que una búsqueda solo lee las listas de sus palabras y no
> toda la tabla. El índice puede rankear de dos formas:
>
> - **TF-IDF con coseno:** da más peso a las palabras raras y luego divide por el largo del
>   documento.
> - **BM25:** también premia las palabras raras, pero repetir una palabra deja de sumar después
>   de cierto punto, y castiga menos el largo.
>
> En mi parte comparé las dos formas entre sí y contra el índice GIN de PostgreSQL.

**Ahora lo probamos en la web.** Ejecuto **[3]**, que crea el índice, y luego **[4]**, que
busca *table lamp* con TF-IDF.

**Qué se ve:**
- La columna de relevancia se muestra con una **barra**, y las palabras buscadas aparecen
  **resaltadas** en el texto. Esa parte de la web la hice yo.
- Con TF-IDF, después de "White Table Lamp" salen **mesas**: "Rivet End Tables",
  "Coffee Table"…

**Qué digo:**
> TF-IDF trae mesas que ni siquiera dicen "lamp". Pasa porque son títulos muy cortos: al
> dividir por el largo, una sola palabra que coincide ya les da un puntaje alto.

**Ejecuto [5]: la misma consulta con BM25.**

**Qué se ve:** las 5 son **lámparas de mesa**.

**Qué digo:**
> BM25 corrige ese problema, y lo medí con 30 consultas evaluadas:
>
> | | Precisión en el top 10 |
> |---|---|
> | TF-IDF | 0,36 |
> | **BM25** | **0,51** |
> | GIN de PostgreSQL | 0,53 |
>
> BM25 queda casi igual que PostgreSQL y tarda lo mismo que TF-IDF, porque los dos leen las
> mismas listas del índice.

**Ejecuto [6]: *bar stool* con BM25.**

**Qué se ve:** 9 de los 10 resultados son bancos de bar (`STOOL_SEATING`).

**Ejecuto [7]: el `EXPLAIN ANALYZE`.**

**Qué se ve:** el nodo **Text Scan using productos_desc**, que lee unos **126 postings** y
unas **12 páginas** en lugar de recorrer los 10 000 productos.

**Qué digo:**
> En mi comparación con 100 000 productos, en consultas de 5 palabras **nuestro índice
> respondió en 8 ms y GIN en 49 ms**. GIN solo sabe *qué* filas tienen las palabras; para
> rankearlas, PostgreSQL tiene que leer cada fila. Nuestro índice saca el puntaje de las
> mismas listas. A cambio, GIN ocupa un tercio del espacio, porque comprime sus listas.

**Ejecuto [8]: *running shoes* solo entre los productos de tipo `SHOES`.**

**Qué se ve:** zapatillas Symactive. La primera es el producto **75**; acuérdense de él para la
Parte 4.

---

## 3. Parte 4: búsqueda por imagen, índice IVF

**Qué digo:**
> En la Parte 4 cada foto se convierte en un **vector de 256 números**.
>
> 1. Con SIFT se detectan puntos de interés en la imagen.
> 2. Cada punto se asigna a una de 256 "palabras visuales".
> 3. Se cuenta cuántas veces aparece cada palabra, igual que con TF-IDF en texto.
>
> Dos fotos se parecen si sus vectores apuntan hacia el mismo lado, y eso lo mide la
> **distancia coseno**.

**Ejecuto [9]:** creo la tabla `fotos` y cargo los 10 000 vectores con `COPY`.

**Ejecuto [10]: buscar con la foto del banco de bar (producto 5), sin índice.**

**Qué se ve:**
- Una **galería de miniaturas** ordenadas por parecido, con la foto, el nombre y el score.
  Esa galería la hice yo.
- El primero es el mismo producto 5 (score 1), y después vienen **5 bancos de bar**.

**Qué digo:**
> Esto es el k-NN secuencial: el motor compara la foto contra los 10 000 vectores.

**Ejecuto [11]: el plan.**

**Qué se ve:** **Seq Vector Scan**, que lee unas **5 000 páginas** en unos **70 ms**.

**Qué digo, para explicar el IVF (lo hice yo):**
> El IVF aplica la idea del índice invertido a los vectores:
>
> 1. Con **k-means** se agrupan los 10 000 vectores en **100 grupos** (`nlist`), cada uno con
>    su centro.
> 2. Cada vector se guarda en la lista de su grupo.
> 3. Para buscar, se compara la consulta solo con los 100 centros, se eligen los **8 grupos
>    más cercanos** (`nprobe`) y se revisan solo esos vectores, unos 800 en lugar de 10 000.
>
> El riesgo es que, si el vecino verdadero está en un grupo que no se revisó, se pierde. Por
> eso `nprobe` es un control entre **velocidad y exactitud**: si lo subo, encuentro más vecinos
> pero tarda más.

**Ejecuto [12]**, que crea el IVF con `nlist = 100` y `nprobe = 8`, y luego **[13]**, la misma
búsqueda `USING IVF`.

**Qué se ve:** **los mismos 6 resultados** que sin índice.

**Ejecuto [14]: el plan con IVF.**

**Qué se ve:** **Vector Scan using fotos_ivf (IVF, coseno, nprobe=8)**, con unas **14 páginas** y
alrededor de **0,2 ms**, contra 5 000 páginas y 70 ms sin índice.

**Qué digo, con la comparación de mi parte sobre 100 000 imágenes:**
> | Técnica | Tiempo | Vecinos correctos (recall) |
> |---|---|---|
> | k-NN secuencial del motor | 488 ms | 99,6 % |
> | IVF, nprobe = 8 | 0,58 ms | 93 % |
> | IVF, nprobe = 32 | 3 ms | 99 % |
> | HNSW | 0,42 ms | 98 % |
>
> Los índices son **cientos de veces más rápidos** a cambio de perder algunos vecinos.
> - El IVF **se construye 7 veces más rápido** que el HNSW (1,8 s contra 13,5 s) y deja ajustar
>   el recall con `nprobe`.
> - El HNSW da más recall al mismo tiempo, pero ocupa 27 % más de memoria.
> - Comparé además las métricas: con coseno el HNSW funciona mejor, y por eso la usamos.

**Un éxito y un error:**

**Ejecuto [15]: la foto de un mocasín (producto 133).**

**Qué se ve:** 4 de 5 vecinos son zapatos.

**Ejecuto [16]: la zapatilla gris del producto 75**, la misma que encontramos por texto en [8].

**Qué se ve:** una alfombra, galletas, vitaminas, una funda de celular y un cuadro.

**Qué digo:**
> Por texto encontramos esta zapatilla sin problema, pero por imagen falla. La foto es una
> zapatilla **lisa sobre fondo blanco**:
> - SIFT casi no encuentra puntos de interés en superficies lisas;
> - las palabras visuales no saben de forma ni de color.
>
> Por eso se parece a cualquier foto de fondo blanco. Es el límite de esta técnica. El índice
> no tiene la culpa: el k-NN exacto da el mismo resultado.

**Ejecuto [17]: imagen más filtro.** Los 6 más parecidos al banco, pero solo de tipo
`STOOL_SEATING`.

**Qué se ve:** 6 bancos. El mueble que salía en [10] ya no aparece.

---

## 4. La interfaz (mi parte del frontend)

**Qué digo:**
> La web reconoce el tipo de consulta y muestra el resultado de la forma más clara:
> - con `MATCH`, barra de score y palabras resaltadas (ya lo vimos);
> - con búsqueda por imagen, la galería (ya la vimos);
> - con k vecinos en el mapa, se numeran los vecinos.

**Ejecuto [18]: las 3 tiendas más cercanas a UTEC.**

**Qué se ve:** en el mapa, el punto de UTEC, los vecinos **numerados 1, 2 y 3** (Barranco,
Miraflores y Chorrillos) y una línea desde UTEC hasta cada uno.

**Para cerrar:**
> En resumen:
> - en texto, **BM25** es la mejor opción del motor;
> - en imágenes, los índices **IVF y HNSW** hacen la búsqueda cientos de veces más rápida;
> - la calidad depende de la foto: con objetos lisos SIFT falla.
>
> Todo está medido en el informe, en las secciones 8.8 y 8.9.

Al terminar, ejecuto **[19]** para borrar las tablas.

---

### Si me preguntan

- **¿Por qué el IVF no da 100 %?** Porque solo revisa 8 de los 100 grupos. Con `nprobe = 100`
  revisa todos y da lo mismo que el exacto.
- **¿Por qué `fotos` es `USING BPLUS`?** FAISS devuelve solo los `id`. Con BPLUS el motor trae
  esas 6 filas por clave, en pocas páginas; con un heap tendría que recorrer toda la tabla
  para encontrarlas.
- **¿Por qué el motor es más lento que FAISS sin índice?** El motor lee del disco página por
  página; FAISS tiene todo en memoria. Lo que se compara es cómo crece cada uno.
- **¿Por qué ABO y no otro dataset?** Tiene texto e imagen del mismo producto, licencia libre y
  categorías que sirven para medir si un resultado es correcto.
