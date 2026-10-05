# Minigestor BD2
#
#   make gui       compila el motor y abre la interfaz
#   make motor     solo el binario .build/motor_sql
#   make test      compila y corre todas las pruebas
#   make bench     benchmarks de heap, secuencial y B+ agrupado (1k / 10k / 100k)
#   make bench-indices  comparacion B+ agrupado / no agrupado / hash con graficas
#   make clean
#
# En Windows: mingw32-make gui (g++ de MinGW en el PATH).

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -pthread
BUILD    := .build
OBJ      := $(BUILD)/obj

ifeq ($(OS),Windows_NT)
  # siempre cmd.exe, aunque haya un sh.exe (Git Bash) en el PATH
  SHELL  := cmd.exe
  .SHELLFLAGS := /C
  EXE    := .exe
  PYTHON ?= python
  MKDIR   = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
  RMDIR   = if exist "$(subst /,\,$1)" rmdir /s /q "$(subst /,\,$1)"
  RMFILE  = del /q $(subst /,\,$1) 2>NUL
  RUN     = $(subst /,\,$1)
else
  EXE    :=
  PYTHON ?= python3
  MKDIR   = mkdir -p $1
  RMDIR   = rm -rf $1
  RMFILE  = rm -f $1
  RUN     = $1
endif

# --- fuentes del motor (sin main) ---
SRC_MOTOR := \
  motor/archivos/pagina_slotted.cpp \
  motor/archivos/heap_file.cpp \
  motor/archivos/sequential_file.cpp \
  motor/indices/bplus_agrupado.cpp \
  motor/indices/bplus_no_agrupado.cpp \
  motor/indices/buffer_pool.cpp \
  motor/indices/gestor_paginas.cpp \
  motor/indices/extendible_hash.cpp \
  motor/consultas/external_algorithms.cpp \
  motor/consultas/parser_sql.cpp \
  motor/consultas/catalogo.cpp \
  motor/consultas/ejecutor.cpp

SRC_AYUDA := motor/pruebas/cargador_csv.cpp

OBJ_MOTOR := $(patsubst %.cpp,$(OBJ)/%.o,$(SRC_MOTOR))
OBJ_AYUDA := $(patsubst %.cpp,$(OBJ)/%.o,$(SRC_AYUDA))

# --- ejecutables ---
MOTOR_SQL := $(BUILD)/motor_sql$(EXE)

TESTS := pagina_slotted_test heap_file_test sequential_file_test bplus_agrupado_test \
         extendible_hash_test hash_disco_test distancia_test rtree_test transacciones_test external_algorithms_test sql_test
BENCHS := heap_file_bench sequential_file_bench bplus_agrupado_csv_test heap_file_escala_test indices_bench \
          extendible_hash_csv_test ejemplo_heap transacciones_demo espacial_bench

TEST_BIN  := $(addprefix $(BUILD)/,$(addsuffix $(EXE),$(TESTS)))
BENCH_BIN := $(addprefix $(BUILD)/,$(addsuffix $(EXE),$(BENCHS)))

.PHONY: all run setup gui motor test bench bench-indices bench-agrupado bench-espacial demo-transacciones clean
.SECONDARY:   # conserva todos los .o intermedios

all: motor

motor: $(MOTOR_SQL)

gui: $(MOTOR_SQL)
	$(PYTHON) api/ui_sql.py

test: $(TEST_BIN)
	@$(call MKDIR,datos/resultados)
	$(call RUN,$(BUILD)/pagina_slotted_test$(EXE))
	$(call RUN,$(BUILD)/heap_file_test$(EXE))
	$(call RUN,$(BUILD)/sequential_file_test$(EXE))
	$(call RUN,$(BUILD)/bplus_agrupado_test$(EXE))
	$(call RUN,$(BUILD)/extendible_hash_test$(EXE))
	$(call RUN,$(BUILD)/hash_disco_test$(EXE))
	$(call RUN,$(BUILD)/distancia_test$(EXE))
	$(call RUN,$(BUILD)/rtree_test$(EXE))
	$(call RUN,$(BUILD)/transacciones_test$(EXE))
	$(call RUN,$(BUILD)/external_algorithms_test$(EXE))
	$(call RUN,$(BUILD)/sql_test$(EXE))
	@echo todo verde

bench: $(BUILD)/heap_file_bench$(EXE) $(BUILD)/sequential_file_bench$(EXE) $(BUILD)/bplus_agrupado_csv_test$(EXE)
	@$(call MKDIR,datos/resultados)
	$(call RUN,$(BUILD)/heap_file_bench$(EXE)) --n 1000   --salida datos/resultados/heap_bench.csv
	$(call RUN,$(BUILD)/heap_file_bench$(EXE)) --n 10000  --salida datos/resultados/heap_bench.csv
	$(call RUN,$(BUILD)/heap_file_bench$(EXE)) --n 100000 --salida datos/resultados/heap_bench.csv
	$(call RUN,$(BUILD)/sequential_file_bench$(EXE)) --n 1000   --salida datos/resultados/sequential_bench.csv
	$(call RUN,$(BUILD)/sequential_file_bench$(EXE)) --n 10000  --salida datos/resultados/sequential_bench.csv
	$(call RUN,$(BUILD)/sequential_file_bench$(EXE)) --n 100000 --salida datos/resultados/sequential_bench.csv
	$(call RUN,$(BUILD)/bplus_agrupado_csv_test$(EXE)) --n 1000   --salida datos/resultados/bplus_agrupado.csv
	$(call RUN,$(BUILD)/bplus_agrupado_csv_test$(EXE)) --n 10000  --salida datos/resultados/bplus_agrupado.csv
	$(call RUN,$(BUILD)/bplus_agrupado_csv_test$(EXE)) --n 100000 --salida datos/resultados/bplus_agrupado.csv
	@echo resultados en datos/resultados/

# comparacion de indices (seccion 2.1.6): corre el bench y regenera las graficas
bench-indices: $(BUILD)/indices_bench$(EXE)
	@$(call MKDIR,datos/resultados)
	-$(call RMFILE,datos/resultados/indices_bench.csv)
	$(call RUN,$(BUILD)/indices_bench$(EXE)) --n 1000
	$(call RUN,$(BUILD)/indices_bench$(EXE)) --n 10000
	$(call RUN,$(BUILD)/indices_bench$(EXE)) --n 100000
	$(call RUN,$(BUILD)/indices_bench$(EXE)) --n 100000 --barajar
	$(PYTHON) docs/comparacion_indices/graficar.py

# comparacion espacial (seccion 2.2.4): busqueda secuencial vs R-Tree con 1k, 10k y 100k puntos
bench-espacial: $(BUILD)/espacial_bench$(EXE)
	@$(call MKDIR,datos/resultados)
	$(call RUN,$(BUILD)/espacial_bench$(EXE))

# transacciones (seccion 2.1.4): hilos con y sin locks sobre la misma cuenta
demo-transacciones: $(BUILD)/transacciones_demo$(EXE)
	@$(call MKDIR,datos/resultados)
	$(call RUN,$(BUILD)/transacciones_demo$(EXE))

# Un solo comando para usuarios nuevos: instala todo, compila y abre la web.
# Equivale a ./bd2 run (en Windows, bd2 run).
run:
	$(PYTHON) tools/motor.py run

setup:
	$(PYTHON) tools/motor.py setup

# B+ agrupado vs heap + B+ no agrupado, a traves del motor SQL
# (ver docs/bplus_agrupado_vs_no_agrupado.md)
bench-agrupado: $(MOTOR_SQL)
	$(PYTHON) api/bench_agrupado.py

# --- reglas ---

$(MOTOR_SQL): $(OBJ)/motor/consultas/motor_sql.o $(OBJ_MOTOR)
	$(CXX) $(CXXFLAGS) $^ -o $@

# cada prueba o bench es motor/pruebas/<nombre>.cpp + el motor + el cargador de CSV
$(BUILD)/%$(EXE): $(OBJ)/motor/pruebas/%.o $(OBJ_MOTOR) $(OBJ_AYUDA)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJ)/%.o: %.cpp
	@$(call MKDIR,$(dir $@))
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	$(call RMDIR,$(OBJ))
	-$(call RMFILE,$(MOTOR_SQL) $(TEST_BIN) $(BENCH_BIN))

-include $(OBJ_MOTOR:.o=.d) $(OBJ_AYUDA:.o=.d) $(wildcard $(OBJ)/motor/pruebas/*.d) $(wildcard $(OBJ)/motor/consultas/motor_sql.d)
