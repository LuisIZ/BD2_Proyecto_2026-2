import { useRef, useState } from "react";
import type { PointerEvent } from "react";
import { structures } from "./structures";
import type { StructureId } from "./structures";

export interface ChartPoint {
  rows: number;
  values: Record<StructureId, number>;
}

const width = 720;
const height = 300;
const margin = { top: 14, right: 20, bottom: 42, left: 62 };
const plotWidth = width - margin.left - margin.right;
const plotHeight = height - margin.top - margin.bottom;

function logDomain(values: number[]) {
  const positive = values.filter((value) => value > 0);
  const low = Math.floor(Math.log10(Math.min(...positive)));
  const high = Math.ceil(Math.log10(Math.max(...positive)));
  return [low, high === low ? low + 1 : high];
}

export default function RangeChart({
  points,
  yLabel,
  format,
}: {
  points: ChartPoint[];
  yLabel: string;
  format: (value: number) => string;
}) {
  const svg = useRef<SVGSVGElement>(null);
  const [hover, setHover] = useState<number>();
  const [xLow, xHigh] = logDomain(points.map((point) => point.rows));
  const [yLow, yHigh] = logDomain(
    points.flatMap((point) => Object.values(point.values)),
  );
  const x = (value: number) =>
    margin.left + ((Math.log10(value) - xLow) / (xHigh - xLow)) * plotWidth;
  const y = (value: number) =>
    margin.top +
    plotHeight -
    ((Math.log10(Math.max(value, 10 ** yLow)) - yLow) / (yHigh - yLow)) *
      plotHeight;
  const decades = (low: number, high: number) =>
    Array.from({ length: high - low + 1 }, (_, i) => 10 ** (low + i));

  function move(event: PointerEvent<SVGRectElement>) {
    const box = svg.current!.getBoundingClientRect();
    const position = ((event.clientX - box.left) / box.width) * width;
    let nearest = 0;
    points.forEach((point, i) => {
      if (
        Math.abs(x(point.rows) - position) <
        Math.abs(x(points[nearest].rows) - position)
      )
        nearest = i;
    });
    setHover(nearest);
  }

  const active = hover === undefined ? undefined : points[hover];
  const left = active ? (x(active.rows) / width) * 100 : 0;

  return (
    <div className="chart">
      <ul className="legend">
        {structures.map((s) => (
          <li key={s.id}>
            <span className="line-key" style={{ background: s.color }} />
            {s.label}
          </li>
        ))}
      </ul>
      <div className="chart-scroll">
        <div className="chart-area">
          <svg
            ref={svg}
            viewBox={`0 0 ${width} ${height}`}
            role="img"
            aria-label={`${yLabel} según filas del rango`}
          >
            {decades(yLow, yHigh).map((tick) => (
              <g key={`y${tick}`}>
                <line
                  className="grid"
                  x1={margin.left}
                  x2={width - margin.right}
                  y1={y(tick)}
                  y2={y(tick)}
                />
                <text
                  className="tick"
                  x={margin.left - 8}
                  y={y(tick) + 4}
                  textAnchor="end"
                >
                  {tick.toLocaleString("es-PE")}
                </text>
              </g>
            ))}
            {decades(xLow, xHigh).map((tick) => (
              <text
                key={`x${tick}`}
                className="tick"
                x={x(tick)}
                y={height - margin.bottom + 18}
                textAnchor="middle"
              >
                {tick.toLocaleString("es-PE")}
              </text>
            ))}
            <text
              className="axis-label"
              x={margin.left + plotWidth / 2}
              y={height - 6}
              textAnchor="middle"
            >
              filas en el rango (escala log)
            </text>
            <text
              className="axis-label"
              transform={`translate(14 ${margin.top + plotHeight / 2}) rotate(-90)`}
              textAnchor="middle"
            >
              {yLabel} (escala log)
            </text>
            {active && (
              <line
                className="crosshair"
                x1={x(active.rows)}
                x2={x(active.rows)}
                y1={margin.top}
                y2={margin.top + plotHeight}
              />
            )}
            {structures.map((s) => (
              <g key={s.id}>
                <polyline
                  fill="none"
                  stroke={s.color}
                  strokeWidth={2}
                  strokeLinejoin="round"
                  strokeLinecap="round"
                  points={points
                    .map((point) => `${x(point.rows)},${y(point.values[s.id])}`)
                    .join(" ")}
                />
                {points.map((point, i) => (
                  <circle
                    key={point.rows}
                    cx={x(point.rows)}
                    cy={y(point.values[s.id])}
                    r={i === hover ? 5 : 4}
                    fill={s.color}
                    stroke="#fff"
                    strokeWidth={2}
                  />
                ))}
              </g>
            ))}
            <rect
              className="hit-area"
              x={margin.left - 10}
              y={margin.top}
              width={plotWidth + 20}
              height={plotHeight}
              onPointerMove={move}
              onPointerLeave={() => setHover(undefined)}
            />
          </svg>
          {active && (
            <div
              className={`chart-tooltip ${left > 60 ? "flip" : ""}`}
              style={{ left: `${left}%` }}
            >
              <strong>{active.rows.toLocaleString("es-PE")} filas</strong>
              {structures.map((s) => (
                <span key={s.id}>
                  <i style={{ background: s.color }} />
                  {s.label}
                  <b>{format(active.values[s.id])}</b>
                </span>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
