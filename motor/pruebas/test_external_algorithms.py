import unittest
import tempfile
from pathlib import Path

from motor.consultas import (
    ExternalHashAggregate,
    ExternalMergeSort,
    JoinPlanner,
    PlanTrace,
    cerrar_log,
    hash_join,
)


class ExternalAlgorithmsTest(unittest.TestCase):
    def test_external_sort_usa_runs_y_merge_k_way(self):
        trace = PlanTrace()
        sorter = ExternalMergeSort(buffer_pages=3, records_per_page=2, k=2, trace=trace)
        records = [{"id": value} for value in [9, 1, 7, 2, 6, 3, 8, 4, 5]]

        resultado = sorter.sort(records, key=lambda record: record["id"])

        self.assertEqual([record["id"] for record in resultado], list(range(1, 10)))
        plan = trace.last("external_merge_sort")
        self.assertEqual(plan["buffer_pages"], 3)
        self.assertEqual(plan["k"], 2)
        self.assertGreater(plan["initial_runs"], 1)

        with self.assertRaises(ValueError):
            ExternalMergeSort(buffer_pages=10, k=10)

    def test_external_sort_100000_registros_con_10_paginas(self):
        trace = PlanTrace()
        sorter = ExternalMergeSort(buffer_pages=10, records_per_page=1000, trace=trace)
        records = range(99_999, -1, -1)

        resultado = sorter.sort(records)

        self.assertEqual(resultado, list(range(100_000)))
        plan = trace.last("external_merge_sort")
        self.assertEqual(plan["buffer_pages"], 10)
        self.assertEqual(plan["k"], 9)
        self.assertEqual(plan["records"], 100_000)
        self.assertGreater(plan["initial_runs"], 1)

    def test_external_hash_aggregate_calcula_agregados(self):
        trace = PlanTrace()
        aggregate = ExternalHashAggregate(
            buffer_pages=4,
            records_per_page=2,
            trace=trace,
        )
        records = [("a", 2), ("b", 10), ("a", 4), ("b", 20), ("a", 6)]

        resultado = aggregate.aggregate(
            records,
            group_key=lambda record: record[0],
            value=lambda record: record[1],
            aggregates={
                "count": "COUNT",
                "sum": "SUM",
                "avg": "AVG",
                "min": "MIN",
                "max": "MAX",
            },
        )

        self.assertEqual(resultado["a"], {"count": 3, "sum": 12, "avg": 4.0, "min": 2, "max": 6})
        self.assertEqual(resultado["b"], {"count": 2, "sum": 30, "avg": 15.0, "min": 10, "max": 20})
        self.assertEqual(trace.last("external_hash_aggregate")["partitions"], 3)

    def test_external_hash_funciona_con_mas_grupos_que_memoria(self):
        aggregate = ExternalHashAggregate(buffer_pages=4, records_per_page=2)
        records = [(group, group) for group in range(100)]

        resultado = aggregate.aggregate(
            records,
            group_key=lambda record: record[0],
            value=lambda record: record[1],
            aggregates={"count": "COUNT", "sum": "SUM"},
        )

        self.assertEqual(len(resultado), 100)
        self.assertEqual(resultado[73], {"count": 1, "sum": 73})
        self.assertGreater(len(resultado), aggregate.max_groups_in_memory)

    def test_hash_join_y_planner_index_nested_loop(self):
        left = [(1, "L1"), (2, "L2"), (3, "L3")]
        right = {1: [(1, "R1")], 2: [(2, "R2")], 4: [(4, "R4")]}
        trace = PlanTrace()
        planner = JoinPlanner(trace)

        resultado_index = planner.execute(
            left,
            [(1, "R1"), (2, "R2"), (4, "R4")],
            outer_key=lambda record: record[0],
            inner_key=lambda record: record[0],
            index=right.get,
        )
        resultado_hash = hash_join(
            left,
            [(1, "R1"), (2, "R2"), (4, "R4")],
            lambda record: record[0],
            lambda record: record[0],
        )

        self.assertEqual(resultado_index, [((1, "L1"), (1, "R1")), ((2, "L2"), (2, "R2"))])
        self.assertEqual(resultado_hash, resultado_index)
        self.assertEqual(trace.events[0]["algorithm"], "index_nested_loop_join")

    def test_planner_elige_hash_join_sin_indice(self):
        trace = PlanTrace()
        planner = JoinPlanner(trace)

        algoritmo = planner.choose(100, 10, index_available=False)

        self.assertEqual(algoritmo, "hash_join")
        self.assertEqual(trace.last()["algorithm"], "hash_join")

    def test_guarda_traza_en_archivo_log_separado(self):
        with tempfile.TemporaryDirectory() as directorio:
            ruta_log = Path(directorio) / "algoritmos-externos.log"
            try:
                trace = PlanTrace(log_path=ruta_log)
                sorter = ExternalMergeSort(
                    buffer_pages=3,
                    records_per_page=1,
                    trace=trace,
                )
                sorter.sort([3, 1, 2])
                JoinPlanner(trace).choose(10, 20, index_available=False)

                contenido = ruta_log.read_text(encoding="utf-8")
                self.assertIn("external_merge_sort", contenido)
                self.assertIn("hash_join", contenido)
            finally:
                cerrar_log(ruta_log)


if __name__ == "__main__":
    unittest.main()