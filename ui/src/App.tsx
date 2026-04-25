import type { Component } from "solid-js";
import { Knob } from "./components/Knob";
import { FreezeButton } from "./components/FreezeButton";

const App: Component = () => {
  return (
    <div class="flex flex-col h-full w-full">
      <header class="pt-6 pb-4 text-center">
        <h1 class="text-xl font-semibold tracking-[0.3em] uppercase text-[color:var(--color-fg)]">
          Spectral Freeze
        </h1>
        <p class="text-xs text-[color:var(--color-fg-dim)] mt-1 tracking-widest uppercase">
          STFT · Phase Vocoder · Magnitude Filter
        </p>
      </header>

      <main class="flex-1 flex items-center justify-center">
        <div class="flex flex-col gap-8 items-center">
          <FreezeButton paramId="freeze" />
          <div class="flex flex-wrap justify-center gap-6 max-w-[680px]">
            <Knob
              paramId="filter"
              label="Filter"
              format={(v) => `${Math.round(v * 100)}%`}
            />
            <Knob
              paramId="scBoost"
              label="SC Boost"
              format={(v) => `+${v.toFixed(1)} dB`}
            />
            <Knob
              paramId="scFreqSmoothing"
              label="SC Smooth"
              format={(v) => `${Math.round(v * 100)}%`}
            />
          </div>
        </div>
      </main>
    </div>
  );
};

export default App;
