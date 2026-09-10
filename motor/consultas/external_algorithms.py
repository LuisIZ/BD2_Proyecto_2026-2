"""Algoritmos externos para ordenar, agrupar y unir relaciones.

Los algoritmos usan archivos temporales para que el limite de memoria sea
explicito. ``buffer_pages`` representa el presupuesto de paginas disponibles
y ``records_per_page`` traduce ese presupuesto a cantidad de registros.
"""

from __future__ import annotations

import heapq
import itertools
import logging
import pickle
import tempfile
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, Iterator, List, Optional, Sequence, Tuple


KeyFunction = Callable[[Any], Any]
logger = logging.getLogger(__name__)


def configurar_log(ruta: Optional[str | Path] = None) -> Path:
    """Configura el archivo persistente del historial de algoritmos externos."""
    destino = Path(ruta) if ruta is not None else Path(__file__).parents[2] / "logs" / "external_algorithms.log"
    destino.parent.mkdir(parents=True, exist_ok=True)
    destino = destino.resolve()
    for handler in logger.handlers:
        if isinstance(handler, logging.FileHandler) and Path(handler.baseFilename) == destino:
            return destino

    handler = logging.FileHandler(destino, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)
    return destino


def cerrar_log(ruta: Optional[str | Path] = None) -> None:
    """Cierra y retira handlers de archivo del historial externo."""
    destino = Path(ruta).resolve() if ruta is not None else None
    for handler in list(logger.handlers):
        if not isinstance(handler, logging.FileHandler):
            continue
        if destino is not None and Path(handler.baseFilename) != destino:
            continue
        logger.removeHandler(handler)
        handler.close()


@dataclass
class PlanTrace:
    """Traza estructurada de decisiones y fases del plan."""

    events: List[dict[str, Any]] = field(default_factory=list)
    log_path: Optional[str | Path] = None

    def __post_init__(self) -> None:
        configurar_log(self.log_path)

    def record(self, algorithm: str, **details: Any) -> None:
        event = {"algorithm": algorithm, **details}
        self.events.append(event)
        logger.info("Plan externo: %s", event)

    def last(self, algorithm: Optional[str] = None) -> dict[str, Any]:
        events = self.events
        if algorithm is not None:
            events = [event for event in events if event["algorithm"] == algorithm]
        if not events:
            raise LookupError("La traza no contiene eventos")
        return events[-1]


class _HeapEntry:
    def __init__(self, key: Any, sequence: int, record: Any, source: int, reverse: bool):
        self.key = key
        self.sequence = sequence
        self.record = record
        self.source = source
        self.reverse = reverse

    def __lt__(self, other: "_HeapEntry") -> bool:
        if self.key != other.key:
            return self.key > other.key if self.reverse else self.key < other.key
        return self.sequence < other.sequence


class ExternalMergeSort:
    """External merge sort con generacion de runs y merge k-way."""

    def __init__(
        self,
        buffer_pages: int = 10,
        records_per_page: int = 100,
        k: Optional[int] = None,
        trace: Optional[PlanTrace] = None,
        log_path: Optional[str | Path] = None,
    ) -> None:
        if buffer_pages < 3:
            raise ValueError("Se requieren al menos 3 paginas de buffer")
        if records_per_page < 1:
            raise ValueError("records_per_page debe ser positivo")
        self.buffer_pages = buffer_pages
        self.records_per_page = records_per_page
        self.k = buffer_pages - 1 if k is None else k
        if self.k < 2 or self.k > buffer_pages - 1:
            raise ValueError("k debe estar entre 2 y buffer_pages - 1")
        if trace is None:
            self.trace = PlanTrace(log_path=log_path)
        else:
            self.trace = trace
            configurar_log(log_path or trace.log_path)

    def sort(
        self,
        records: Iterable[Any],
        key: Optional[KeyFunction] = None,
        reverse: bool = False,
    ) -> List[Any]:
        key_function = key or (lambda record: record)
        chunk_size = self.buffer_pages * self.records_per_page
        with tempfile.TemporaryDirectory(prefix="external-sort-") as directory:
            run_paths = self._generate_runs(records, key_function, reverse, chunk_size, directory)
            initial_runs = len(run_paths)
            merge_passes = 0
            while len(run_paths) > self.k:
                run_paths = self._merge_pass(run_paths, key_function, reverse, directory)
                merge_passes += 1
            result = self._merge_runs(run_paths, key_function, reverse)
            self.trace.record(
                "external_merge_sort",
                buffer_pages=self.buffer_pages,
                records_per_page=self.records_per_page,
                k=self.k,
                initial_runs=initial_runs,
                merge_passes=merge_passes,
                records=len(result),
            )
            return result

    def _generate_runs(
        self,
        records: Iterable[Any],
        key: KeyFunction,
        reverse: bool,
        chunk_size: int,
        directory: str,
    ) -> List[Path]:
        paths: List[Path] = []
        iterator = iter(records)
        for run_number in itertools.count():
            chunk = list(itertools.islice(iterator, chunk_size))
            if not chunk:
                return paths
            chunk.sort(key=key, reverse=reverse)
            path = Path(directory) / f"run-{run_number}.bin"
            self._write_records(path, chunk)
            paths.append(path)

    def _merge_pass(
        self,
        paths: Sequence[Path],
        key: KeyFunction,
        reverse: bool,
        directory: str,
    ) -> List[Path]:
        merged_paths: List[Path] = []
        for group_number, start in enumerate(range(0, len(paths), self.k)):
            group = paths[start : start + self.k]
            output = Path(directory) / f"merge-{group_number}-{len(merged_paths)}.bin"
            self._write_records(output, self._merge_runs(group, key, reverse))
            merged_paths.append(output)
        return merged_paths

    def _merge_runs(self, paths: Sequence[Path], key: KeyFunction, reverse: bool) -> List[Any]:
        if not paths:
            return []
        handles = [path.open("rb") for path in paths]
        heap: List[_HeapEntry] = []
        sequence = itertools.count()
        try:
            for source, handle in enumerate(handles):
                record = self._read_record(handle)
                if record is not None:
                    heapq.heappush(
                        heap,
                        _HeapEntry(key(record), next(sequence), record, source, reverse),
                    )
            result: List[Any] = []
            while heap:
                entry = heapq.heappop(heap)
                result.append(entry.record)
                record = self._read_record(handles[entry.source])
                if record is not None:
                    heapq.heappush(
                        heap,
                        _HeapEntry(key(record), next(sequence), record, entry.source, reverse),
                    )
            return result
        finally:
            for handle in handles:
                handle.close()

    @staticmethod
    def _write_records(path: Path, records: Iterable[Any]) -> None:
        with path.open("wb") as handle:
            for record in records:
                pickle.dump(record, handle, protocol=pickle.HIGHEST_PROTOCOL)

    @staticmethod
    def _read_record(handle: Any) -> Any:
        try:
            return pickle.load(handle)
        except EOFError:
            return None


class ExternalHashAggregate:
    """GROUP BY externo mediante particiones hash y agregados por grupo."""

    VALID_AGGREGATES = {"COUNT", "SUM", "AVG", "MIN", "MAX"}

    def __init__(
        self,
        buffer_pages: int = 10,
        records_per_page: int = 100,
        trace: Optional[PlanTrace] = None,
        log_path: Optional[str | Path] = None,
    ) -> None:
        if buffer_pages < 3:
            raise ValueError("Se requieren al menos 3 paginas de buffer")
        if records_per_page < 1:
            raise ValueError("records_per_page debe ser positivo")
        self.buffer_pages = buffer_pages
        self.records_per_page = records_per_page
        self.partition_count = buffer_pages - 1
        self.max_groups_in_memory = buffer_pages * records_per_page
        if trace is None:
            self.trace = PlanTrace(log_path=log_path)
        else:
            self.trace = trace
            configurar_log(log_path or trace.log_path)

    def aggregate(
        self,
        records: Iterable[Any],
        group_key: KeyFunction,
        aggregates: Dict[str, Any],
        value: Optional[KeyFunction] = None,
    ) -> Dict[Any, Dict[str, Any]]:
        operations = {
            aggregate.upper() if isinstance(aggregate, str) else aggregate
            for aggregate in aggregates.values()
        }
        invalid = operations - self.VALID_AGGREGATES
        if invalid:
            raise ValueError(f"Agregados no soportados: {sorted(invalid)}")
        value_function = value or (lambda record: record)
        with tempfile.TemporaryDirectory(prefix="external-hash-") as directory:
            raw_path = Path(directory) / "input.bin"
            count = 0
            with raw_path.open("wb") as raw_handle:
                for record in records:
                    key = group_key(record)
                    pickle.dump((key, value_function(record)), raw_handle)
                    count += 1

            partition_count = max(
                self.partition_count,
                (count + self.max_groups_in_memory - 1) // self.max_groups_in_memory,
            )
            paths = [Path(directory) / f"partition-{index}.bin" for index in range(partition_count)]
            handles = [path.open("wb") for path in paths]
            try:
                with raw_path.open("rb") as raw_handle:
                    while True:
                        try:
                            group, item = pickle.load(raw_handle)
                        except EOFError:
                            break
                        pickle.dump(
                            (group, item),
                            handles[hash(group) % partition_count],
                        )
            finally:
                for handle in handles:
                    handle.close()

            result: Dict[Any, Dict[str, Any]] = {}
            for path in paths:
                partial = self._aggregate_partition(path, aggregates)
                result.update(partial)
            self.trace.record(
                "external_hash_aggregate",
                buffer_pages=self.buffer_pages,
                partitions=partition_count,
                max_groups_in_memory=self.max_groups_in_memory,
                aggregates=sorted(aggregates),
                records=count,
                groups=len(result),
            )
            return result

    def _aggregate_partition(self, path: Path, aggregates: Dict[str, Any]) -> Dict[Any, Dict[str, Any]]:
        states: Dict[Any, Dict[str, Any]] = {}
        with path.open("rb") as handle:
            while True:
                try:
                    group, item = pickle.load(handle)
                except EOFError:
                    break
                state = states.setdefault(group, {"__count": 0, "__sum": 0, "__min": None, "__max": None})
                state["__count"] += 1
                state["__sum"] += item if item is not None else 0
                state["__min"] = item if state["__min"] is None else min(state["__min"], item)
                state["__max"] = item if state["__max"] is None else max(state["__max"], item)
        result: Dict[Any, Dict[str, Any]] = {}
        for group, state in states.items():
            values: Dict[str, Any] = {}
            for output_name, aggregate in aggregates.items():
                operation = aggregate.upper() if isinstance(aggregate, str) else aggregate
                if operation == "COUNT":
                    values[output_name] = state["__count"]
                elif operation == "SUM":
                    values[output_name] = state["__sum"]
                elif operation == "AVG":
                    values[output_name] = state["__sum"] / state["__count"]
                elif operation == "MIN":
                    values[output_name] = state["__min"]
                elif operation == "MAX":
                    values[output_name] = state["__max"]
            result[group] = values
        return result


def hash_join(
    left: Iterable[Any],
    right: Iterable[Any],
    left_key: KeyFunction,
    right_key: KeyFunction,
) -> List[Tuple[Any, Any]]:
    """Hash join equijoinando ambas relaciones por sus claves."""
    hash_table: Dict[Any, List[Any]] = defaultdict(list)
    for record in right:
        hash_table[right_key(record)].append(record)
    return [
        (left_record, right_record)
        for left_record in left
        for right_record in hash_table.get(left_key(left_record), [])
    ]


def index_nested_loop_join(
    outer: Iterable[Any],
    index: Any,
    outer_key: KeyFunction,
) -> List[Tuple[Any, Any]]:
    """Index nested loop usando ``buscar`` o una funcion de lookup."""
    lookup = index.buscar if hasattr(index, "buscar") else index
    result: List[Tuple[Any, Any]] = []
    for outer_record in outer:
        matches = lookup(outer_key(outer_record))
        if matches is None:
            continue
        if isinstance(matches, list):
            result.extend((outer_record, match) for match in matches)
        else:
            result.append((outer_record, matches))
    return result


class JoinPlanner:
    """Selecciona hash join o index nested loop y registra la decision."""

    def __init__(
        self,
        trace: Optional[PlanTrace] = None,
        log_path: Optional[str | Path] = None,
    ) -> None:
        if trace is None:
            self.trace = PlanTrace(log_path=log_path)
        else:
            self.trace = trace
            configurar_log(log_path or trace.log_path)

    def choose(
        self,
        outer_rows: int,
        inner_rows: int,
        index_available: bool,
    ) -> str:
        if index_available and outer_rows <= inner_rows:
            algorithm = "index_nested_loop_join"
        else:
            algorithm = "hash_join"
        self.trace.record(
            algorithm,
            outer_rows=outer_rows,
            inner_rows=inner_rows,
            index_available=index_available,
        )
        return algorithm

    def execute(
        self,
        outer: Sequence[Any],
        inner: Sequence[Any],
        outer_key: KeyFunction,
        inner_key: KeyFunction,
        index: Any = None,
    ) -> List[Tuple[Any, Any]]:
        algorithm = self.choose(len(outer), len(inner), index is not None)
        if algorithm == "index_nested_loop_join":
            result = index_nested_loop_join(outer, index, outer_key)
        else:
            result = hash_join(outer, inner, outer_key, inner_key)
        self.trace.record(f"{algorithm}_execute", rows=len(result))
        return result