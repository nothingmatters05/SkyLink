import { useCallback, useEffect, useRef, useState } from "react";
import { parseTelemetry } from "./parseTelemetry";
import type { LinkStatus, TelemetrySample } from "../types/telemetry";

const STALE_AFTER_MS = 2000;
const CONNECT_TIMEOUT_MS = 5000;
const RECONNECT_DELAY_MS = 1500;

export function useTelemetrySocket(enabled: boolean, url: string) {
  const [sample, setSample] = useState<TelemetrySample | null>(null);
  const [status, setStatus] = useState<LinkStatus>("disconnected");
  const [error, setError] = useState("");
  const [lastPacketAt, setLastPacketAt] = useState<number | null>(null);
  const [lastPacketValid, setLastPacketValid] = useState<boolean | null>(null);
  const socketRef = useRef<WebSocket | null>(null);
  const reconnectTimerRef = useRef<number | null>(null);
  const connectionTimerRef = useRef<number | null>(null);
  const retryRef = useRef(true);

  const disconnect = useCallback(() => {
    retryRef.current = false;
    if (reconnectTimerRef.current !== null) {
      window.clearTimeout(reconnectTimerRef.current);
      reconnectTimerRef.current = null;
    }
    if (connectionTimerRef.current !== null) {
      window.clearTimeout(connectionTimerRef.current);
      connectionTimerRef.current = null;
    }
    socketRef.current?.close();
    socketRef.current = null;
    setStatus("disconnected");
    setError("");
  }, []);

  useEffect(() => {
    if (!enabled) {
      disconnect();
      return;
    }

    retryRef.current = true;
    let disposed = false;
    const connect = () => {
      if (disposed || !retryRef.current) return;
      setStatus("connecting");
      setError("");
      setLastPacketValid(null);

      let socket: WebSocket;
      try {
        socket = new WebSocket(url);
      } catch (reason) {
        setStatus("error");
        setError(reason instanceof Error ? reason.message : "Invalid WebSocket URL");
        reconnectTimerRef.current = window.setTimeout(connect, RECONNECT_DELAY_MS);
        return;
      }

      socketRef.current = socket;
      connectionTimerRef.current = window.setTimeout(() => {
        if (socket.readyState !== WebSocket.CONNECTING) return;
        setStatus("error");
        setError(`Connection timed out after ${CONNECT_TIMEOUT_MS / 1000} seconds`);
        socket.close();
      }, CONNECT_TIMEOUT_MS);
      socket.addEventListener("open", () => {
        if (disposed || !retryRef.current || socketRef.current !== socket) {
          socket.close();
          return;
        }
        if (connectionTimerRef.current !== null) {
          window.clearTimeout(connectionTimerRef.current);
          connectionTimerRef.current = null;
        }
        setStatus("connected");
        setError("");
      });
      socket.addEventListener("message", (event: MessageEvent<string>) => {
        if (disposed || !retryRef.current || socketRef.current !== socket) return;
        if (typeof event.data !== "string") {
          setLastPacketValid(false);
          setError("Received a non-text telemetry frame");
          return;
        }
        try {
          const parsed = parseTelemetry(event.data);
          const receivedAt = Date.now();
          setSample({ ...parsed, receivedAt });
          setLastPacketAt(receivedAt);
          setLastPacketValid(true);
          setStatus("connected");
          setError("");
        } catch (reason) {
          setLastPacketValid(false);
          setError(reason instanceof Error ? reason.message : "Invalid telemetry packet");
        }
      });
      socket.addEventListener("error", () => {
        if (disposed || !retryRef.current || socketRef.current !== socket) return;
        setError(`WebSocket transport error at ${url}; check the ESP32 address, port, and Wi-Fi link`);
      });
      socket.addEventListener("close", () => {
        if (connectionTimerRef.current !== null) {
          window.clearTimeout(connectionTimerRef.current);
          connectionTimerRef.current = null;
        }
        if (socketRef.current === socket) socketRef.current = null;
        if (disposed || !retryRef.current) return;
        setStatus("connecting");
        reconnectTimerRef.current = window.setTimeout(connect, RECONNECT_DELAY_MS);
      });
    };

    connect();
    return () => {
      disposed = true;
      retryRef.current = false;
      if (reconnectTimerRef.current !== null) {
        window.clearTimeout(reconnectTimerRef.current);
      }
      if (connectionTimerRef.current !== null) {
        window.clearTimeout(connectionTimerRef.current);
      }
      socketRef.current?.close();
      socketRef.current = null;
    };
  }, [enabled, url, disconnect]);

  useEffect(() => {
    if (!enabled || lastPacketAt === null) return;
    const timer = window.setInterval(() => {
      if (Date.now() - lastPacketAt > STALE_AFTER_MS) setStatus("stale");
    }, 250);
    return () => window.clearInterval(timer);
  }, [enabled, lastPacketAt]);

  return { sample, status, error, lastPacketValid, disconnect };
}
