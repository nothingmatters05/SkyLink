interface ImportMetaEnv {
  readonly DEV: boolean;
  readonly VITE_TELEMETRY_WS_URL?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
