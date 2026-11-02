import { useEffect, useRef, useState } from "react";
import {
  Activity,
  Antenna,
  ArrowDownToLine,
  Camera,
  ChevronDown,
  CircleHelp,
  Compass,
  Crosshair,
  Gauge,
  PanelRightClose,
  Play,
  RotateCcw,
  Settings2,
  SlidersHorizontal,
  Square,
  Wifi,
} from "lucide-react";
import DroneScene from "./components/DroneScene";
import TelemetryChart, { type HistoryPoint } from "./components/TelemetryChart";
import { useTelemetrySocket } from "./communication/useTelemetrySocket";
import { DEFAULT_AXIS_MAPPING, mapTelemetry, type AxisMapping } from "./simulation/axisMapping";
import { ALTITUDE_LIMIT_METERS, ALTITUDE_MARKS_METERS } from "./simulation/flightEnvelope";
import type { DemoControls, LinkStatus, OperatingMode, TelemetrySample } from "./types/telemetry";

const initialDemo: DemoControls = {
  roll: 0,
  pitch: 0,
  yaw: 0,
  altitude: 0.3,
  autoMotion: true,
};

function statusLabel(status: LinkStatus) {
  const labels: Record<LinkStatus, string> = {
    disconnected: "Disconnected",
    connecting: "Connecting",
    connected: "Receiving",
    stale: "Signal stale",
    error: "Link error",
  };
  return labels[status];
}

function format(value: number, precision = 1) {
  return Number.isFinite(value) ? value.toFixed(precision) : "—";
}

function wrapHeading(value: number) {
  return ((value % 360) + 360) % 360;
}

function App() {
  const [mode, setMode] = useState<OperatingMode>("DEMO");
  const [demo, setDemo] = useState(initialDemo);
  const [running, setRunning] = useState(true);
  const [graphsVisible, setGraphsVisible] = useState(true);
  const [panelVisible, setPanelVisible] = useState(true);
  const [cameraMode, setCameraMode] = useState<"follow" | "free">("follow");
  const [resetViewKey, setResetViewKey] = useState(0);
  const [axisMapping, setAxisMapping] = useState<AxisMapping>(DEFAULT_AXIS_MAPPING);
  const [liveAltitudeOffset, setLiveAltitudeOffset] = useState(0);
  const [liveRollOffset, setLiveRollOffset] = useState(0);
  const [livePitchOffset, setLivePitchOffset] = useState(0);
  const [socketUrl, setSocketUrl] = useState("ws://192.168.4.1:81");
  const [liveEnabled, setLiveEnabled] = useState(false);
  const [demoSample, setDemoSample] = useState<TelemetrySample>({
    roll: 0, pitch: 0, yawRate: 0, altitude: 0.3, receivedAt: Date.now(),
  });
  const [heading, setHeading] = useState(0);
  const [history, setHistory] = useState<HistoryPoint[]>([]);
  const [receiveRate, setReceiveRate] = useState<number | null>(null);
  const lastDemoUpdateAt = useRef<number | null>(null);
  const lastLiveSampleAt = useRef<number | null>(null);
  const previousPacketAt = useRef<number | null>(null);
  const { sample: liveSample, status, error, disconnect } = useTelemetrySocket(liveEnabled, socketUrl);

  const sourceTelemetry = mode === "DEMO" ? demoSample : liveSample;
  const referencedTelemetry = sourceTelemetry
    ? {
      ...sourceTelemetry,
      roll: sourceTelemetry.roll - (mode === "LIVE" ? liveRollOffset : 0),
      pitch: sourceTelemetry.pitch - (mode === "LIVE" ? livePitchOffset : 0),
      altitude: sourceTelemetry.altitude - (mode === "LIVE" ? liveAltitudeOffset : 0),
    }
    : null;
  const telemetry = referencedTelemetry ? mapTelemetry(referencedTelemetry, axisMapping) : null;
  const sampleAge = telemetry ? Date.now() - telemetry.receivedAt : Infinity;
  const isFresh = mode === "DEMO" || sampleAge <= 2000;
  const linkDisplay = status === "connected" && !telemetry
    ? "Waiting for telemetry"
    : telemetry && !isFresh
      ? "Signal stale"
      : statusLabel(status);
  const altitude = telemetry?.altitude ?? 0;
  const outsideRange = Math.abs(altitude) > ALTITUDE_LIMIT_METERS;
  const displayedAltitude = Math.min(ALTITUDE_LIMIT_METERS, Math.max(-ALTITUDE_LIMIT_METERS, altitude));
  const telemetryRef = useRef(telemetry);
  telemetryRef.current = telemetry;

  useEffect(() => {
    if (!running || mode !== "DEMO") return;
    const started = performance.now();
    lastDemoUpdateAt.current = started;
    const timer = window.setInterval(() => {
      const now = performance.now();
      const elapsed = (now - started) / 1000;
      const delta = Math.min(Math.max((now - (lastDemoUpdateAt.current ?? now)) / 1000, 0), 0.1);
      lastDemoUpdateAt.current = now;
      const motion = demo.autoMotion;
      const next = {
        roll: demo.roll + (motion ? Math.sin(elapsed * 0.58) * 8 : 0),
        pitch: demo.pitch + (motion ? Math.sin(elapsed * 0.39 + 0.8) * 5 : 0),
        yawRate: motion ? Math.cos(elapsed * 0.3) * 13 : 0,
        altitude: demo.altitude + (motion ? Math.sin(elapsed * 0.22) * 0.35 : 0),
        receivedAt: Date.now(),
      };
      setDemoSample(next);
      setHeading((value) => wrapHeading(value + next.yawRate * delta));
    }, 50);
    return () => window.clearInterval(timer);
  }, [demo, mode, running]);

  useEffect(() => {
    if (mode !== "LIVE") {
      lastLiveSampleAt.current = null;
      return;
    }
    if (!liveSample) {
      lastLiveSampleAt.current = null;
      return;
    }

    const previousAt = lastLiveSampleAt.current;
    lastLiveSampleAt.current = liveSample.receivedAt;
    if (!running || previousAt === null) return;

    const elapsed = Math.min(Math.max((liveSample.receivedAt - previousAt) / 1000, 0), 0.2);
    setHeading((value) => wrapHeading(value + liveSample.yawRate * elapsed));
  }, [liveSample, mode, running]);

  useEffect(() => {
    const timer = window.setInterval(() => {
      const current = telemetryRef.current;
      if (!current) return;
      const now = new Date();
      setHistory((points) => [
        ...points.slice(-119),
        {
          time: now.toLocaleTimeString([], { minute: "2-digit", second: "2-digit" }),
          roll: current.roll,
          pitch: current.pitch,
          yaw: current.yawRate,
          altitude: current.altitude,
        },
      ]);
    }, 250);
    return () => window.clearInterval(timer);
  }, []);

  useEffect(() => {
    if (mode === "DEMO") {
      previousPacketAt.current = null;
      setReceiveRate(20);
      return;
    }
    if (!telemetry) {
      previousPacketAt.current = null;
      setReceiveRate(null);
      return;
    }
    const previousAt = previousPacketAt.current;
    previousPacketAt.current = telemetry.receivedAt;
    if (previousAt === null) return;
    const interval = telemetry.receivedAt - previousAt;
    if (interval > 0) {
      const currentRate = 1000 / interval;
      setReceiveRate((rate) => rate === null ? currentRate : rate * 0.75 + currentRate * 0.25);
    }
  }, [mode, telemetry?.receivedAt]);

  function changeMode(next: OperatingMode) {
    setMode(next);
    if (next === "LIVE") setLiveEnabled(true);
    else {
      setLiveEnabled(false);
      disconnect();
    }
  }

  function resetAltitudeReference() {
    if (mode === "DEMO") {
      setDemo((current) => ({ ...current, altitude: 0 }));
      setDemoSample((current) => ({ ...current, altitude: 0 }));
    } else if (liveSample) {
      setLiveAltitudeOffset(liveSample.altitude);
    }
  }

  function resetOrientation() {
    setHeading(0);
    if (mode === "DEMO") {
      setDemo((current) => ({ ...current, roll: 0, pitch: 0, yaw: 0 }));
      setDemoSample((current) => ({ ...current, roll: 0, pitch: 0, yawRate: 0 }));
    } else if (liveSample) {
      setLiveRollOffset(liveSample.roll);
      setLivePitchOffset(liveSample.pitch);
    }
  }

  function updateDemo(key: keyof DemoControls, value: number | boolean) {
    setDemo((current) => ({ ...current, [key]: value }));
  }

  function updateMapping(update: Partial<AxisMapping>) {
    setAxisMapping((current) => ({ ...current, ...update }));
  }

  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="brand-lockup">
          <div className="brand-mark"><Crosshair size={19} strokeWidth={1.7} /></div>
          <div>
            <div className="brand-name">SKYLINK<span> / </span>FLIGHT SYSTEMS</div>
            <div className="brand-caption">UAV motion simulator</div>
          </div>
        </div>
        <div className="topbar-center">
          <div className={`connection-pill ${mode === "DEMO" || (status === "connected" && isFresh) ? "is-live" : ""}`}>
            <span className="status-dot" />
            <span>{mode === "DEMO" ? "DEMO SIGNAL" : linkDisplay.toUpperCase()}</span>
          </div>
          <div className={`mode-pill mode-${mode.toLowerCase()}`}>{mode} MODE</div>
        </div>
        <button className="icon-button" aria-label="Settings" title="Connection settings" onClick={() => document.getElementById("socket-url")?.focus()}>
          <Settings2 size={17} />
        </button>
      </header>

      <section className="page-heading">
        <div>
          <p className="eyebrow"><span className="eyebrow-line" />GROUND CONTROL · LOCAL FLIGHT SIMULATION</p>
          <h1>Flight deck</h1>
        </div>
        <div className="heading-actions">
          <label className="mode-select-label" htmlFor="mode-select">INPUT SOURCE</label>
          <div className="select-wrap">
            <select id="mode-select" value={mode} onChange={(event) => changeMode(event.target.value as OperatingMode)}>
              <option value="DEMO">Demo telemetry</option>
              <option value="LIVE">Live · ESP32 WebSocket</option>
            </select>
            <ChevronDown size={14} />
          </div>
        </div>
      </section>

      <section className={`flight-layout ${panelVisible ? "" : "panel-collapsed"}`}>
        <div className="simulation-column">
          <section className="viewport-panel">
            <div className="viewport-topline">
              <div className="view-title"><span className="tiny-crosshair"><Crosshair size={14} /></span>3D SIMULATION <span className="subtle-separator">/</span> WORLD VIEW</div>
              <div className="viewport-tools">
                <span className="axis-key"><i className="axis-x" />X</span>
                <span className="axis-key"><i className="axis-y" />Y</span>
                <span className="axis-key"><i className="axis-z" />Z</span>
                <button className="panel-toggle" onClick={() => setPanelVisible((visible) => !visible)} title={panelVisible ? "Hide telemetry" : "Show telemetry"}>
                  <PanelRightClose size={15} />
                </button>
              </div>
            </div>
            <div className="viewport">
              <DroneScene
                roll={telemetry?.roll ?? 0}
                pitch={telemetry?.pitch ?? 0}
                heading={wrapHeading(heading * axisMapping.yawSign)}
                altitude={displayedAltitude}
                running={running}
                cameraMode={cameraMode}
                resetViewKey={resetViewKey}
              />
              <div className="viewport-hud hud-top-left">
                <div className="hud-label"><span className="live-indicator" />{mode} · {running ? "SIMULATION ACTIVE" : "SIMULATION PAUSED"}</div>
                <div className="hud-readout">ATTITUDE <b>R {format(telemetry?.roll ?? 0)}°</b> <b>P {format(telemetry?.pitch ?? 0)}°</b></div>
              </div>
              <div className="altitude-ruler" aria-label={`Altitude reference, minus ${ALTITUDE_LIMIT_METERS} to plus ${ALTITUDE_LIMIT_METERS} metres`}>
                <span className="altitude-ruler-title">REL ALT · m</span>
                <div className="altitude-ruler-scale">
                  {ALTITUDE_MARKS_METERS.map((mark) => (
                    <span className={mark === 0 ? "altitude-zero" : ""} key={mark}>
                      <i />{mark > 0 ? "+" : ""}{mark.toFixed(1)}
                    </span>
                  ))}
                  <i className="altitude-current" style={{ top: `${((ALTITUDE_LIMIT_METERS - displayedAltitude) / (ALTITUDE_LIMIT_METERS * 2)) * 100}%` }} />
                </div>
              </div>
              <div className="viewport-hud hud-bottom-left">
                <span>ALTITUDE WINDOW</span><strong>−{ALTITUDE_LIMIT_METERS.toFixed(1)} <i /> +{ALTITUDE_LIMIT_METERS.toFixed(1)} m</strong>
                {outsideRange && <em>OUTSIDE DISPLAY RANGE · ACTUAL {format(altitude, 2)} m</em>}
              </div>
              <div className="viewport-hud hud-bottom-right">
                <Compass size={13} />
                <span>YAW {format(wrapHeading(heading * axisMapping.yawSign), 0)}° RELATIVE</span>
              </div>
            </div>
            <div className="viewport-footer">
              <span><i className="footer-dot" />{mode === "DEMO" ? "SIMULATED DATA · NOT HARDWARE TELEMETRY" : isFresh ? "LIVE DATA STREAM" : "NO FRESH TELEMETRY"}</span>
              <span>DRAG TO ORBIT <b>·</b> SCROLL TO ZOOM <b>·</b> RIGHT-DRAG TO PAN</span>
            </div>
          </section>

          <div className="control-strip">
            <div className="control-group">
              <button className={`primary-control ${running ? "active" : ""}`} onClick={() => setRunning((value) => !value)}>
                {running ? <Square size={13} fill="currentColor" /> : <Play size={14} fill="currentColor" />}
                {running ? "PAUSE SIM" : "START SIM"}
              </button>
              <button className="secondary-control" onClick={resetOrientation} disabled={mode === "LIVE" && (!liveSample || !isFresh)}><RotateCcw size={14} />RESET ATTITUDE</button>
              <button className="secondary-control" onClick={() => setCameraMode((value) => value === "follow" ? "free" : "follow")}><Camera size={14} />{cameraMode === "follow" ? "FOLLOW CAM" : "FREE CAM"}</button>
              <button className="secondary-control" onClick={() => setResetViewKey((value) => value + 1)}><Crosshair size={14} />RESET VIEW</button>
            </div>
            <div className="control-status"><span className={running ? "status-ready" : ""} />{running ? "SYSTEM RUNNING" : "SIMULATION PAUSED"}</div>
          </div>

          {mode === "DEMO" && (
            <section className="demo-panel">
              <div className="section-heading">
                <div><SlidersHorizontal size={15} /><h2>DEMO FLIGHT INPUT</h2></div>
                <label className="toggle-label">
                  <input type="checkbox" checked={demo.autoMotion} onChange={(event) => updateDemo("autoMotion", event.target.checked)} />
                  <span className="toggle-track" />
                  AUTO MOTION
                </label>
              </div>
              <div className="slider-grid">
                <Slider label="ROLL" value={demo.roll} min={-35} max={35} unit="°" onChange={(value) => updateDemo("roll", value)} />
                <Slider label="PITCH" value={demo.pitch} min={-35} max={35} unit="°" onChange={(value) => updateDemo("pitch", value)} />
                <Slider label="YAW" value={demo.yaw} min={-180} max={180} unit="°" onChange={(value) => { updateDemo("yaw", value); setHeading(value); }} />
                <Slider label="ALTITUDE" value={demo.altitude} min={-ALTITUDE_LIMIT_METERS} max={ALTITUDE_LIMIT_METERS} step={0.05} unit="m" onChange={(value) => updateDemo("altitude", value)} />
              </div>
            </section>
          )}

          {graphsVisible && (
            <section className="graph-panel">
              <div className="section-heading">
                <div><Activity size={15} /><h2>TELEMETRY HISTORY</h2><span className="window-tag">30 SEC WINDOW</span></div>
                <span className={`chart-status ${isFresh ? "chart-live" : "chart-stale"}`}><span />{!telemetry ? "WAITING FOR DATA" : isFresh ? "UPDATING" : "SIGNAL STALE"}</span>
              </div>
              <TelemetryChart data={history} />
              <div className="graph-note">Yaw displays integrated relative heading rate · Sensor axes not transmitted by firmware</div>
            </section>
          )}
        </div>

        {panelVisible && (
          <aside className="telemetry-column">
            <section className="side-panel primary-readings">
              <div className="section-heading">
                <div><Gauge size={15} /><h2>FLIGHT TELEMETRY</h2></div>
                <span className={`mini-status ${mode === "DEMO" || isFresh ? "good" : "waiting"}`}><i />{mode === "DEMO" ? "DEMO" : isFresh ? "LIVE" : "STALE"}</span>
              </div>
              <div className="telemetry-hero">
                <div className="hero-heading"><span>RELATIVE ALTITUDE</span><button className="text-button" onClick={resetAltitudeReference} disabled={mode === "LIVE" && (!liveSample || !isFresh)} title={mode === "LIVE" ? "Set the browser display reference to the current STM32 altitude" : "Set demo altitude to zero"}><ArrowDownToLine size={12} /> ZERO</button></div>
                <div className="altitude-number">{format(altitude, 2)}<span>m</span></div>
                <div className={`altitude-range ${outsideRange ? "out-of-range" : ""}`}>
                  <div className="range-track"><span className="range-center" /><span className="range-indicator" style={{ left: `${((displayedAltitude + ALTITUDE_LIMIT_METERS) / (ALTITUDE_LIMIT_METERS * 2)) * 100}%` }} /></div>
                  <div className="range-labels"><span>−{ALTITUDE_LIMIT_METERS} m</span><span>0 m</span><span>+{ALTITUDE_LIMIT_METERS} m</span></div>
                </div>
                {outsideRange && <div className="range-warning">Actual altitude is outside the displayed ±{ALTITUDE_LIMIT_METERS} m range.</div>}
              </div>
              <div className="attitude-grid">
                <Metric label="ROLL" value={telemetry?.roll} unit="°" color="cyan" />
                <Metric label="PITCH" value={telemetry?.pitch} unit="°" color="amber" />
                <Metric label="YAW RATE" value={telemetry?.yawRate} unit="°/s" color="violet" />
                <Metric label="HEADING" value={wrapHeading(heading * axisMapping.yawSign)} unit="° rel." color="green" />
              </div>
            </section>

            <section className="side-panel link-panel">
              <div className="section-heading">
                <div><Antenna size={15} /><h2>TELEMETRY LINK</h2></div>
                <span className="small-action"><CircleHelp size={13} /></span>
              </div>
              <div className="link-status-row">
                <span className={`link-icon ${mode === "DEMO" || isFresh ? "link-good" : ""}`}><Wifi size={16} /></span>
                <div><strong>{mode === "DEMO" ? "Demo source" : linkDisplay}</strong><small>{mode === "DEMO" ? "Local simulation" : socketUrl}</small></div>
                <span className={`link-state ${mode === "DEMO" || isFresh ? "state-on" : ""}`}>{mode === "DEMO" ? "ACTIVE" : linkDisplay.toUpperCase()}</span>
              </div>
              <div className="socket-config">
                <label htmlFor="socket-url">WEBSOCKET URL</label>
                <div className="url-input-wrap">
                  <input id="socket-url" value={socketUrl} onChange={(event) => setSocketUrl(event.target.value)} placeholder="ws://192.168.4.1:81" spellCheck={false} />
                  <button onClick={() => {
                    if (mode === "LIVE" && liveEnabled) {
                      disconnect();
                      setLiveEnabled(false);
                    } else {
                      setMode("LIVE");
                      setLiveEnabled(true);
                    }
                  }}>{liveEnabled ? "DISCONNECT" : "CONNECT"}</button>
                </div>
                {error && <p className="link-error">{error}</p>}
              </div>
              <div className="packet-detail">
                <span>RX RATE <strong>{receiveRate === null ? "—" : `${receiveRate.toFixed(1)} Hz`}</strong></span>
                <span>LAST PACKET <strong>{telemetry && isFresh ? "VALID · R/P/Y/ALT" : "WAITING"}</strong></span>
              </div>
            </section>

            <section className="side-panel sensor-panel">
              <div className="section-heading">
                <div><Activity size={15} /><h2>SENSOR AVAILABILITY</h2></div>
                <span className="expand-label">FIRMWARE FIELDS <ChevronDown size={12} /></span>
              </div>
              <div className="sensor-row"><span>MPU · ATTITUDE</span><b className={mode === "DEMO" || isFresh ? "available" : ""}><i />{mode === "DEMO" ? "SIMULATED" : isFresh ? "AVAILABLE" : "NO SIGNAL"}</b></div>
              <div className="sensor-row"><span>BMP280 · RELATIVE ALT.</span><b className={mode === "DEMO" || isFresh ? "available" : ""}><i />{mode === "DEMO" ? "SIMULATED" : isFresh ? "AVAILABLE" : "NO SIGNAL"}</b></div>
              <div className="sensor-row"><span>BARO REFERENCE</span><b className={mode === "DEMO" || isFresh ? "available" : ""}><i />{mode === "DEMO" ? "SIMULATED" : isFresh ? "STM32 STARTUP" : "WAITING"}</b></div>
              <div className="sensor-footnote">MPU6050-compatible register map · raw accel/gyro axes are not sent over UART.</div>
              <details className="axis-mapping">
                <summary>ATTITUDE AXIS MAPPING <ChevronDown size={12} /></summary>
                <div className="mapping-row">
                  <label htmlFor="roll-source">MODEL ROLL FROM</label>
                  <select id="roll-source" value={axisMapping.rollSource} onChange={(event) => updateMapping({ rollSource: event.target.value as AxisMapping["rollSource"] })}>
                    <option value="roll">Sensor roll</option><option value="pitch">Sensor pitch</option>
                  </select>
                  <button onClick={() => updateMapping({ rollSign: axisMapping.rollSign === 1 ? -1 : 1 })}>{axisMapping.rollSign === 1 ? "+" : "−"}</button>
                </div>
                <div className="mapping-row">
                  <label htmlFor="pitch-source">MODEL PITCH FROM</label>
                  <select id="pitch-source" value={axisMapping.pitchSource} onChange={(event) => updateMapping({ pitchSource: event.target.value as AxisMapping["pitchSource"] })}>
                    <option value="pitch">Sensor pitch</option><option value="roll">Sensor roll</option>
                  </select>
                  <button onClick={() => updateMapping({ pitchSign: axisMapping.pitchSign === 1 ? -1 : 1 })}>{axisMapping.pitchSign === 1 ? "+" : "−"}</button>
                </div>
                <div className="mapping-row">
                  <label>YAW RATE SIGN</label><span className="mapping-hint">Gyroscope · relative only</span>
                  <button onClick={() => updateMapping({ yawSign: axisMapping.yawSign === 1 ? -1 : 1 })}>{axisMapping.yawSign === 1 ? "+" : "−"}</button>
                </div>
              </details>
            </section>
          </aside>
        )}
      </section>

      <footer className="app-footer">
        <div><span className="footer-brand-dot" />SKYLINK <span className="footer-divider">/</span> ENGINEERING SIMULATOR</div>
        <div className="footer-right">
          <button onClick={() => setGraphsVisible((value) => !value)}><Activity size={13} />{graphsVisible ? "HIDE GRAPHS" : "SHOW GRAPHS"}</button>
          <span>PROTOCOL · R / P / Y / ALT</span>
          <span>BUILD 0.1.0</span>
        </div>
      </footer>
    </main>
  );
}

function Metric({ label, value, unit, color }: { label: string; value?: number; unit: string; color: string }) {
  return (
    <div className={`metric-card metric-${color}`}>
      <span>{label}</span>
      <strong>{value === undefined ? "—" : format(value)}<i>{unit}</i></strong>
    </div>
  );
}

function Slider({ label, value, min, max, step = 1, unit, onChange }: {
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  unit: string;
  onChange: (value: number) => void;
}) {
  return (
    <label className="demo-slider">
      <span className="slider-caption">{label}<b>{value.toFixed(unit === "m" ? 2 : 0)}{unit}</b></span>
      <input type="range" min={min} max={max} step={step} value={value} onChange={(event) => onChange(Number(event.target.value))} />
    </label>
  );
}

export default App;
