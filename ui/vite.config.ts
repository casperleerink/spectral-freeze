import { defineConfig } from "vite";
import solid from "vite-plugin-solid";
import tailwindcss from "@tailwindcss/vite";

// `base: "./"` makes every asset path relative, which is what the JUCE
// resource provider needs when it serves the built files at the custom
// juce:// (macOS/iOS/Linux) or https://juce.backend/ (Windows) root.
export default defineConfig({
  plugins: [solid(), tailwindcss()],
  base: "./",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
    cssCodeSplit: false,
    assetsInlineLimit: 0,
  },
  server: {
    port: 5173,
    strictPort: true,
  },
});
