param(
    [switch]$SoloCompilar,
    [switch]$ConEscala,
    [switch]$ConBench
)

$ErrorActionPreference = "Stop"

$comunes = "-std=c++17", "-Wall", "-Wextra", "-pedantic"
$depuracion = $comunes + @("-g", "-DHEAP_DEBUG", "-D_GLIBCXX_ASSERTIONS")
$optimizado = $comunes + @("-O2")

$pagina = "motor/archivos/pagina_slotted.cpp"
$heap = "motor/archivos/heap_file.cpp"
$cargador = "motor/pruebas/cargador_csv.cpp"

New-Item -ItemType Directory -Force .build, datos/resultados | Out-Null

function Compilar($banderas, $fuentes, $salida) {
    Write-Host "compilando $salida" -ForegroundColor Cyan
    & g++ @banderas @fuentes -o $salida
    if ($LASTEXITCODE -ne 0) {
        Write-Host "fallo la compilacion de $salida" -ForegroundColor Red
        exit 1
    }
}

function Ejecutar($ruta) {
    Write-Host ""
    Write-Host "== $ruta ==" -ForegroundColor Yellow
    & $ruta
    if ($LASTEXITCODE -ne 0) {
        Write-Host "fallo $ruta (exit $LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
}

Compilar $depuracion @($pagina, "motor/pruebas/pagina_slotted_test.cpp") `
    ".build/pagina_slotted_test.exe"

Compilar $depuracion @($pagina, $heap, "motor/pruebas/heap_file_test.cpp") `
    ".build/heap_file_test.exe"

Compilar $optimizado @($pagina, $heap, $cargador, "motor/pruebas/heap_file_escala_test.cpp") `
    ".build/heap_file_escala_test.exe"

Compilar $optimizado @($pagina, $heap, $cargador, "motor/pruebas/heap_file_bench.cpp") `
    ".build/heap_file_bench.exe"

if ($SoloCompilar) {
    Write-Host ""
    Write-Host "los cuatro ejecutables estan en .build/" -ForegroundColor Green
    exit 0
}

Ejecutar ".\.build\pagina_slotted_test.exe"
Ejecutar ".\.build\heap_file_test.exe"

if ($ConEscala) {
    Ejecutar ".\.build\heap_file_escala_test.exe"
}

if ($ConBench) {
    foreach ($n in 1000, 10000, 100000) {
        & .\.build\heap_file_bench.exe --n $n --salida datos/resultados/heap_bench.csv
        & .\.build\heap_file_bench.exe --n $n --barajar --salida datos/resultados/heap_bench.csv
    }
    Write-Host ""
    Write-Host "resultados en datos/resultados/heap_bench.csv" -ForegroundColor Green
}

Write-Host ""
Write-Host "todo verde" -ForegroundColor Green
