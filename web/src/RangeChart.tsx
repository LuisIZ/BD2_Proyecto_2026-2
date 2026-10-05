import { useRef, useState } from "react";
import type { PointerEvent } from "react";

export interface ChartPoint {
  rows: number;
  values: Record<string, number>;
}

export interface ChartSeries {
  id: string;
  label: string;
  color: string;
}

const width = 720;
const height = 300;
const margin = { top: 14, right: 20, bottom: 42, left: 62 };
const plotWidth = width - margin.left - margin.right;
const plotHeight = height - margin.top - margin.bottom;

function logDomain(values: number[]) {
  const positive = values.filter((value) => value > 0);
  if (!positive.length) return [0, 1];
  const low = Math.floor(Math.log10(Math.min(...positive)));
  const high = Math.ceil(Math.log10(Math.max(...positive)));
  return [low, high === low ? low + 1 : high];
}

export default function RangeChart({
  points,
  series,
  yLabel,
  format,
  logarithmic = true,
}: {
  points: ChartPoint[];
  series: ChartSeries[];
  yLabel: string;
  format: (value: number) => string;
  logarithmic?: boolean;
}) {
  const svg = useRef<SVGSVGElement>(null);
  const [hover, setHover] = useState<number>();
  const [xLow, xHigh] = logDomain(points.map((point) => point.rows));
  const yValues = points.flatMap((point) =>
    series.map((item) => point.values[item.id]).filter(Number.isFinite),
  );
  const positiveYValues = yValues.filter((value) => value > 0);
  const logY = logarithmic && positiveYValues.length > 0;
  const [yLow, yHigh] = logDomain(yValues);
  const yMax = Math.max(1, ...yValues);
  const x = (value: number) =>
    margin.left + ((Math.log10(value) - xLow) / (xHigh - xLow)) * plotWidth;
  const y = (value: number) =>
    margin.top +
    plotHeight -
    (logY
      ? ((Math.log10(Math.max(value, 10 ** yLow)) - yLow) / (yHigh - yLow)) *
        plotHeight
      : (Math.max(0, value) / yMax) * plotHeight);
  const decades = (low: number, high: number) =>
    Array.from({ length: high - low + 1 }, (_, i) => 10 ** (low + i));
  const yTicks = logY ? decades(yLow, yHigh) : [0, yMax / 2, yMax];

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
        {series.map((item) => (
          <li key={item.id}>
            <span className="line-key" style={{ background: item.color }} />
            {item.label}
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
            {yTicks.map((tick) => (
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
              {yLabel}
              {logY ? " (escala log)" : " (escala lineal)"}
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
            {series.map((item) => (
              <g key={item.id}>
                <polyline
                  fill="none"
                  stroke={item.color}
                  strokeWidth={2}
                  strokeLinejoin="round"
                  strokeLinecap="round"
                  points={points
                    .map(
                      (point) =>
                        `${x(point.rows)},${y(point.values[item.id] ?? 0)}`,
                    )
                    .join(" ")}
                />
                {points.map((point, i) => (
                  <circle
                    key={point.rows}
                    cx={x(point.rows)}
                    cy={y(point.values[item.id] ?? 0)}
                    r={i === hover ? 5 : 4}
                    fill={item.color}
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
              {series.map((item) => (
                <span key={item.id}>
                  <i style={{ background: item.color }} />
                  {item.label}
                  <b>{format(active.values[item.id] ?? 0)}</b>
                </span>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
