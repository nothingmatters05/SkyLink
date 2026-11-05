import { createHash } from "node:crypto";
import { defineConfig } from "vite";
import type { Plugin } from "vite";
import react from "@vitejs/plugin-react";

const chunkAliases: Record<string, string> = {
  charts: "charts",
  index: "app",
  "react-three": "r3f",
  "react-vendor": "react",
  "three-core": "three",
  "three-helpers": "helpers",
};

const shortHashedAssetNames: Plugin = {
  name: "short-hex-asset-names",
  enforce: "post",
  generateBundle(_options, bundle) {
    const outputs = Object.values(bundle);
    const originalNames = outputs.map((output) => output.fileName);
    const nameMap = new Map<string, string>();
    const targets = new Set<string>();
    const normalizedNames = new Map(
      originalNames.map((fileName, index) => [
        fileName.split("/").pop()!,
        `__SKYLINK_ASSET_${index}__`,
      ]),
    );

    for (const output of outputs) {
      if (output.type === "asset" && output.fileName === "index.html") continue;

      const originalName = output.fileName;
      const extensionIndex = originalName.lastIndexOf(".");
      const extension = extensionIndex >= 0 ? originalName.slice(extensionIndex) : "";
      const outputName = output.type === "chunk"
        ? output.name
        : output.name ?? originalName.slice("assets/".length, extensionIndex);
      const sourceName = extension && outputName.endsWith(extension)
        ? outputName.slice(0, -extension.length)
        : outputName;
      const alias = chunkAliases[sourceName]
        ?? (sourceName.replace(/[^a-zA-Z0-9_-]/g, "").slice(0, 8) || "asset");
      const maxAliasLength = 30 - "/assets/".length - 1 - 10 - extension.length;
      if (maxAliasLength < 1) {
        this.error(`Asset extension is too long for the 30-byte path limit: ${originalName}`);
      }
      const shortName = alias.slice(0, maxAliasLength);
      let content = output.type === "chunk" ? output.code : output.source;
      if (typeof content === "string") {
        for (const [fileName, token] of normalizedNames) {
          content = content.split(fileName).join(token);
        }
      }
      const hash = createHash("sha256").update(content).digest("hex").slice(0, 10);
      const target = `assets/${shortName}-${hash}${extension}`;
      if (target.length + 1 > 30) {
        this.error(`Generated asset path exceeds 30 bytes: /${target}`);
      }
      if (targets.has(target)) {
        this.error(`Generated asset filename collision: /${target}`);
      }
      targets.add(target);
      nameMap.set(originalName, target);
    }

    const renamedBasenames = new Map(
      [...nameMap].map(([originalName, target]) => [
        originalName.split("/").pop()!,
        target.split("/").pop()!,
      ]),
    );
    for (const output of outputs) {
      if (output.type === "chunk") {
        output.imports = output.imports.map((fileName) => nameMap.get(fileName) ?? fileName);
        output.dynamicImports = output.dynamicImports.map((fileName) => nameMap.get(fileName) ?? fileName);
        for (const [oldName, newName] of renamedBasenames) {
          output.code = output.code.split(oldName).join(newName);
        }
      } else if (typeof output.source === "string") {
        for (const [oldName, newName] of renamedBasenames) {
          output.source = output.source.split(oldName).join(newName);
        }
      }
    }

    for (const [originalName, target] of nameMap) {
      const output = bundle[originalName];
      if (!output) this.error(`Missing generated output: ${originalName}`);

      delete bundle[originalName];
      output.fileName = target;
      bundle[target] = output;
    }
  },
};

export default defineConfig({
  base: "/",
  plugins: [react(), shortHashedAssetNames],
  server: {
    host: "0.0.0.0",
  },
  build: {
    chunkSizeWarningLimit: 700, // Tree-shaken Three.js is ~688 KB raw (~177 KB gzip).
    rollupOptions: {
      output: {
        entryFileNames: "assets/[name].js",
        chunkFileNames: "assets/[name].js",
        assetFileNames: (asset) => asset.name === "index.html"
          ? "[name][extname]"
          : "assets/[name][extname]",
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
