"""Tabla hash extensible en memoria.

La tabla usa los bits menos significativos del hash para consultar un
directorio. Los buckets se dividen cuando alcanzan su capacidad y el
directorio crece solo cuando la profundidad local lo requiere.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Dict, Generic, Hashable, Iterator, List, Optional, Tuple, TypeVar


Clave = TypeVar("Clave", bound=Hashable)
Valor = TypeVar("Valor")


@dataclass
class _Bucket(Generic[Clave, Valor]):
    profundidad_local: int
    registros: Dict[Clave, Valor] = field(default_factory=dict)


class ExtendibleHashing(Generic[Clave, Valor]):
    """Indice hash extensible con insercion, busqueda y eliminacion.

    ``bucket_capacity`` limita la cantidad de registros por bucket. La tabla
    comienza con un unico bucket y el directorio se duplica al dividir un
    bucket cuya profundidad local coincide con la profundidad global.
    """

    def __init__(
        self,
        bucket_capacity: int = 4,
        hash_function: Optional[Callable[[Clave], int]] = None,
        max_depth: int = 64,
    ) -> None:
        if bucket_capacity < 1:
            raise ValueError("La capacidad del bucket debe ser positiva")
        if max_depth < 0:
            raise ValueError("La profundidad maxima no puede ser negativa")

        self.bucket_capacity = bucket_capacity
        self._hash_function = hash_function or hash
        self._max_depth = max_depth
        self._profundidad_global = 0
        self._directorio: List[_Bucket[Clave, Valor]] = [_Bucket(0)]
        self._cantidad = 0

    @property
    def profundidad_global(self) -> int:
        return self._profundidad_global

    @property
    def cantidad(self) -> int:
        return self._cantidad

    @property
    def cantidad_buckets(self) -> int:
        return len({id(bucket) for bucket in self._directorio})

    def supportsRange(self) -> bool:
        """Indica si el indice soporta predicados de rango."""
        return False

    def supports_range(self) -> bool:
        """Alias en snake_case para integraciones Python."""
        return self.supportsRange()

    def insertar(self, clave: Clave, valor: Valor) -> bool:
        """Inserta ``clave`` y ``valor``; devuelve ``False`` si ya existe."""
        if self.contiene(clave):
            return False

        while True:
            bucket = self._bucket_para(clave)
            if len(bucket.registros) < self.bucket_capacity:
                bucket.registros[clave] = valor
                self._cantidad += 1
                return True
            if not self._puede_separar(bucket, clave):
                raise OverflowError("Las claves colisionan en todos los bits disponibles")
            self._dividir(bucket)

    def buscar(self, clave: Clave, default: Optional[Valor] = None) -> Optional[Valor]:
        """Devuelve el valor asociado o ``default`` si la clave no existe."""
        return self._bucket_para(clave).registros.get(clave, default)

    def contiene(self, clave: Clave) -> bool:
        return clave in self._bucket_para(clave).registros

    def eliminar(self, clave: Clave) -> bool:
        """Elimina una clave y fusiona buckets cuando dejan de ser necesarios."""
        bucket = self._bucket_para(clave)
        if clave not in bucket.registros:
            return False

        del bucket.registros[clave]
        self._cantidad -= 1
        self._fusionar(bucket)
        return True

    def items(self) -> List[Tuple[Clave, Valor]]:
        """Devuelve los registros activos sin duplicar buckets referenciados."""
        registros: Dict[Clave, Valor] = {}
        for bucket in self._buckets_unicos():
            registros.update(bucket.registros)
        return list(registros.items())

    def estadisticas(self) -> dict[str, int]:
        return {
            "registros": self._cantidad,
            "profundidad_global": self.profundidad_global,
            "buckets": self.cantidad_buckets,
            "capacidad_bucket": self.bucket_capacity,
        }

    def __len__(self) -> int:
        return self._cantidad

    def __contains__(self, clave: object) -> bool:
        return self.contiene(clave)  # type: ignore[arg-type]

    def __iter__(self) -> Iterator[Tuple[Clave, Valor]]:
        return iter(self.items())

    insertar_record = insertar
    search = buscar
    delete = eliminar

    def _hash(self, clave: Clave) -> int:
        return self._hash_function(clave) & ((1 << self._max_depth) - 1)

    def _indice(self, clave: Clave) -> int:
        if self._profundidad_global == 0:
            return 0
        return self._hash(clave) & ((1 << self._profundidad_global) - 1)

    def _bucket_para(self, clave: Clave) -> _Bucket[Clave, Valor]:
        return self._directorio[self._indice(clave)]

    def _puede_separar(self, bucket: _Bucket[Clave, Valor], clave: Clave) -> bool:
        profundidad = bucket.profundidad_local + 1
        if profundidad > self._max_depth:
            return False
        mascara = (1 << profundidad) - 1
        claves = list(bucket.registros) + [clave]
        indices = {self._hash(otra_clave) & mascara for otra_clave in claves}
        return len(indices) > 1

    def _dividir(self, bucket: _Bucket[Clave, Valor]) -> None:
        if bucket.profundidad_local >= self._max_depth:
            raise OverflowError("Se alcanzo la profundidad maxima del hash")

        if bucket.profundidad_local == self._profundidad_global:
            self._directorio.extend(self._directorio)
            self._profundidad_global += 1

        nueva_profundidad = bucket.profundidad_local + 1
        hermano = _Bucket[Clave, Valor](nueva_profundidad)
        bucket.profundidad_local = nueva_profundidad

        for indice, actual in enumerate(self._directorio):
            if actual is bucket and (indice & (1 << (nueva_profundidad - 1))):
                self._directorio[indice] = hermano

        registros = list(bucket.registros.items())
        bucket.registros.clear()
        for clave, valor in registros:
            destino = self._bucket_para(clave)
            destino.registros[clave] = valor

    def _fusionar(self, bucket: _Bucket[Clave, Valor]) -> None:
        while bucket.profundidad_local > 0:
            indice = next(
                indice
                for indice, actual in enumerate(self._directorio)
                if actual is bucket
            )
            indice_hermano = indice ^ (1 << (bucket.profundidad_local - 1))
            hermano = self._directorio[indice_hermano]
            if hermano is bucket or hermano.profundidad_local != bucket.profundidad_local:
                break
            if len(bucket.registros) + len(hermano.registros) > self.bucket_capacity:
                break

            hermano.registros.update(bucket.registros)
            profundidad = bucket.profundidad_local - 1
            hermano.profundidad_local = profundidad
            for indice, actual in enumerate(self._directorio):
                if actual is bucket:
                    self._directorio[indice] = hermano
            bucket = hermano

        while self._profundidad_global > 0:
            if any(
                bucket.profundidad_local == self._profundidad_global
                for bucket in self._buckets_unicos()
            ):
                break
            mitad = len(self._directorio) // 2
            self._directorio = self._directorio[:mitad]
            self._profundidad_global -= 1

    def _buckets_unicos(self) -> List[_Bucket[Clave, Valor]]:
        unicos: Dict[int, _Bucket[Clave, Valor]] = {}
        for bucket in self._directorio:
            unicos[id(bucket)] = bucket
        return list(unicos.values())