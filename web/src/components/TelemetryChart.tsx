import {
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";

export interface HistoryPoint {
  time: string;
  roll: number;
  pitch: number;
  yaw: number;
  altitude: number;
}

export default function TelemetryChart({ data }: { data: HistoryPoint[] }) {
  return (
    <div className="chart-wrap">
      <div className="chart-legend">
        <span><i className="legend-roll" />Roll</span>
        <span><i className="legend-pitch" />Pitch</span>
        <span><i className="legend-yaw" />Yaw rate</span>
        <span><i className="legend-alt" />Altitude</span>
      </div>
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={data} margin={{ top: 10, right: 12, left: -20, bottom: 0 }}>
          <CartesianGrid stroke="#283335" strokeDasharray="3 5" vertical={false} />
          <XAxis dataKey="time" tick={{ fill: "#718080", fontSize: 10 }} tickLine={false} axisLine={false} minTickGap={35} />
          <YAxis tick={{ fill: "#718080", fontSize: 10 }} tickLine={false} axisLine={false} width={42} />
          <Tooltip contentStyle={{ background: "#151d1f", border: "1px solid #354244", borderRadius: 8, fontSize: 11 }} labelStyle={{ color: "#b8c5c3" }} />
          <Line type="monotone" dataKey="roll" stroke="#62d5d5" dot={false} strokeWidth={1.7} isAnimationActive={false} />
          <Line type="monotone" dataKey="pitch" stroke="#d7a665" dot={false} strokeWidth={1.7} isAnimationActive={false} />
          <Line type="monotone" dataKey="yaw" stroke="#9d8bd4" dot={false} strokeWidth={1.6} isAnimationActive={false} />
          <Line type="monotone" dataKey="altitude" stroke="#80c39d" dot={false} strokeWidth={1.7} isAnimationActive={false} />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}
