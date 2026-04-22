import { createSignal, onCleanup, onMount } from "solid-js";
import { getToggleState, type ToggleState } from "juce-framework-frontend";

export function useToggle(paramId: string) {
  const state: ToggleState = getToggleState(paramId);
  const [value, setValue] = createSignal(state.getValue());

  onMount(() => {
    const id = state.valueChangedEvent.addListener(() =>
      setValue(state.getValue())
    );
    onCleanup(() => state.valueChangedEvent.removeListener(id));
  });

  return {
    value,
    setValue: (v: boolean) => state.setValue(v),
    toggle: () => state.setValue(!state.getValue()),
  };
}
