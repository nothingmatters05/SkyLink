export interface TelemetrySample {
  roll: number;
  pitch: number;
  yawRate: number;
  altitude: number;
  receivedAt: number;
}

export type LinkStatus =
  | "disconnected"
  | "connecting"
  | "connected"
  | "stale"
  | "error";

export type OperatingMode = "DEMO" | "LIVE";

export interface DemoControls {
  roll: number;
  pitch: number;
  yaw: number;
  altitude: number;
  autoMotion: boolean;
}
