import type { TelemetrySample } from "../types/telemetry";

const expectedKeys = new Set(["R", "P", "Y", "Alt"]);

export function parseTelemetry(line: string): Omit<TelemetrySample, "receivedAt"> {
  const values = new Map<string, number>();

  for (const field of line.trim().split(",")) {
    const separator = field.indexOf(":");
    if (separator <= 0) {
      throw new Error("Malformed field; expected key:value");
    }

    const key = field.slice(0, separator).trim();
    const rawValue = field.slice(separator + 1).trim();
    if (!expectedKeys.has(key) || values.has(key) || rawValue.length === 0) {
      throw new Error(`Unexpected or duplicate telemetry field: ${key || "(empty)"}`);
    }

    const value = Number(rawValue);
    if (!Number.isFinite(value)) {
      throw new Error(`Telemetry field ${key} is not a finite number`);
    }
    values.set(key, value);
  }

  if (values.size !== expectedKeys.size) {
    throw new Error("Telemetry packet is missing one or more required fields");
  }

  const roll = values.get("R");
  const pitch = values.get("P");
  const yawRate = values.get("Y");
  const altitude = values.get("Alt");
  if (roll === undefined || pitch === undefined || yawRate === undefined || altitude === undefined) {
    throw new Error("Telemetry packet is missing one or more required fields");
  }

  return {
    roll,
    pitch,
    yawRate,
    altitude,
  };
}
