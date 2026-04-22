import { type Component } from "solid-js";
import { useToggle } from "../hooks/useToggle";

export const FreezeButton: Component<{ paramId: string }> = (props) => {
  const toggle = useToggle(props.paramId);

  return (
    <button
      type="button"
      onClick={() => toggle.toggle()}
      class="px-8 py-3 rounded-full font-semibold tracking-widest uppercase text-sm transition-all duration-150 border"
      classList={{
        "bg-[color:var(--color-accent)] text-[color:var(--color-bg)] border-[color:var(--color-accent)] shadow-[0_0_20px_rgba(124,196,255,0.45)]":
          toggle.value(),
        "bg-[color:var(--color-panel-2)] text-[color:var(--color-fg-dim)] border-[color:var(--color-panel-border)] hover:text-[color:var(--color-fg)]":
          !toggle.value(),
      }}
    >
      {toggle.value() ? "Frozen" : "Freeze"}
    </button>
  );
};
