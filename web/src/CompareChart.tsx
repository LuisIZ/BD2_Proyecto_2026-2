import { useState } from "react";
import { structures } from "./structures";
import type { StructureId } from "./structures";

/**
 * Barras comparativas de las cuatro organizaciones. Se redibuja cada vez que se
 * corre un caso, así que la gráfica siempre refleja la última medición en vez
 * de una tabla que hay que leer número por número.
 *
 * La escala es logarítmica cuando la diferencia pasa de dos órdenes de
 * magnitud: con un heap que lee 3 687 páginas y un B+ que lee 3, en escala
 * lineal las barras cortas desaparecen.
 */

export interface Bar {
  id: StructureId;
  value: number;
  /** texto bajo la barra: la operación que eligió el planificador */
  note?: string;
}

const width = 560;
const height = 230;
const margin = { top: 18, right: 16, bottom: 56, left: 72 };
const plotWidth = width - margin.left - margin.right;
const plotHeight = height - margin.top - margin.bottom;

export default function CompareChart({
  bars,
  title,
  format,
  unit,
}: {
  bars: Bar[];
  title: string;
  format: (value: number) => string;
  unit: string;
}) {
  const [hover, setHover] = useState<number>();
  const values = bars.map((bar) => bar.value);
  const max = Math.max(...values, 0);
  const positive = values.filter((value) => value > 0);
  const min = positive.length ? Math.min(...positive) : 0;
  // con tres órdenes de magnitud entre la mayor y la menor, lineal no sirve
  const log = min > 0 && max / min >= 100;
  const top = max <= 0 ? 1 : max;

  const scale = (value: number) => {
    if (value <= 0) return 0;
    if (!log) return (value / top) * plotHeight;
    const low = Math.log10(Math.max(min, top / 1e6)) - 0.4;
    return ((Math.log10(value) - low) / (Math.log10(top) - low)) * plotHeight;
  };

  const slot = plotWidth / bars.length;
  const barWidth = Math.min(72, slot * 0.56);
  const ticks = log
    ? Array.from(
        { length: Math.ceil(Math.log10(top)) - Math.floor(Math.log10(min)) + 1 },
        (_, i) => 10 ** (Math.floor(Math.log10(min)) + i),
      ).filter((tick) => tick <= top * 1.2)
    : [0, top / 2, top];

  return (
    <figure className="compare-chart">
      <figcaption>
        {title}
        <span className="muted"> · {log ? "escala log" : "escala lineal"}</span>
      </figcaption>
      <svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label={title}>
        {ticks.map((tick, i) => {
          const y = margin.top + plotHeight - scale(tick);
          return (
            <g key={i}>
              <line
                className="grid"
                x1={margin.left}
                x2={width - margin.right}
                y1={y}
                y2={y}
              />
              <text
                className="tick"
                x={margin.left - 8}
                y={y + 4}
                textAnchor="end"
              >
                {format(tick)}
              </text>
            </g>
          );
        })}
        <text
          className="axis-label"
          transform={`translate(14 ${margin.top + plotHeight / 2}) rotate(-90)`}
          textAnchor="middle"
        >
          {unit}
        </text>
        {bars.map((bar, i) => {
          const s = structures.find((item) => item.id === bar.id)!;
          const barHeight = Math.max(bar.value > 0 ? 2 : 0, scale(bar.value));
          const x = margin.left + slot * i + (slot - barWidth) / 2;
          const y = margin.top + plotHeight - barHeight;
          const best = bar.value > 0 && bar.value === Math.min(...positive);
          return (
            <g
              key={bar.id}
              onPointerEnter={() => setHover(i)}
              onPointerLeave={() => setHover(undefined)}
              className={`compare-bar ${hover === i ? "active" : ""}`}
            >
              <rect
                x={x}
                y={y}
                width={barWidth}
                height={barHeight}
                rx={4}
                fill={s.color}
                opacity={hover === undefined || hover === i ? 1 : 0.55}
              />
              <text
                className="bar-value"
                x={x + barWidth / 2}
                y={y - 6}
                textAnchor="middle"
              >
                {format(bar.value)}
                {best ? " ★" : ""}
              </text>
              <text
                className="bar-name"
                x={x + barWidth / 2}
                y={margin.top + plotHeight + 18}
                textAnchor="middle"
              >
                {s.label}
              </text>
              {bar.note && (
                <text
                  className="bar-note"
                  x={x + barWidth / 2}
                  y={margin.top + plotHeight + 33}
                  textAnchor="middle"
                >
                  {bar.note}
                </text>
              )}
            </g>
          );
        })}
        <line
          className="axis"
          x1={margin.left}
          x2={width - margin.right}
          y1={margin.top + plotHeight}
          y2={margin.top + plotHeight}
        />
      </svg>
    </figure>
  );
}
