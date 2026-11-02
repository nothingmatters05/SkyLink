import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  server: {
    host: "0.0.0.0",
  },
  build: {
    chunkSizeWarningLimit: 700, // Tree-shaken Three.js is ~688 KB raw (~177 KB gzip).
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (/\/node_modules\/(react|react-dom|scheduler)\//.test(id)) {
            return "react-vendor";
          }
          if (id.includes("/node_modules/three-stdlib/")) {
            return "three-helpers";
          }
          if (id.includes("/node_modules/three/")) {
            return "three-core";
          }
          if (id.includes("/node_modules/@react-three/")) {
            return "react-three";
          }
          if (id.includes("/node_modules/recharts/")) {
            return "charts";
          }
        },
      },
    },
  },
});
