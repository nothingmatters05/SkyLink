import type { TelemetrySample } from "../types/telemetry";

export type AttitudeAxis = "roll" | "pitch";

export interface AxisMapping {
  rollSource: AttitudeAxis;
  pitchSource: AttitudeAxis;
  rollSign: 1 | -1;
  pitchSign: 1 | -1;
  yawSign: 1 | -1;
}

export const DEFAULT_AXIS_MAPPING: AxisMapping = {
  rollSource: "roll",
  pitchSource: "pitch",
  rollSign: 1,
  // Positive sensor pitch rotates the model nose downward with the scene's axes.
  pitchSign: -1,
  yawSign: 1,
};

export function mapTelemetry(sample: TelemetrySample, mapping: AxisMapping): TelemetrySample {
  return {
    ...sample,
    roll: sample[mapping.rollSource] * mapping.rollSign,
    pitch: sample[mapping.pitchSource] * mapping.pitchSign,
    yawRate: sample.yawRate * mapping.yawSign,
  };
}
