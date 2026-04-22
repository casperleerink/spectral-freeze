import { createSignal, onCleanup, onMount } from "solid-js";
import { getSliderState, type SliderState } from "juce-framework-frontend";

// Wraps a JUCE SliderState in Solid signals so components re-render on change.
export function useSlider(paramId: string) {
  const state: SliderState = getSliderState(paramId);
  const [value, setValue] = createSignal(state.getNormalisedValue());
  const [scaled, setScaled] = createSignal(state.getScaledValue());
  const [props, setProps] = createSignal(state.properties);

  onMount(() => {
    const onValue = state.valueChangedEvent.addListener(() => {
      setValue(state.getNormalisedValue());
      setScaled(state.getScaledValue());
    });
    const onProps = state.propertiesChangedEvent.addListener(() => {
      setProps({ ...state.properties });
      setScaled(state.getScaledValue());
      setValue(state.getNormalisedValue());
    });
    onCleanup(() => {
      state.valueChangedEvent.removeListener(onValue);
      state.propertiesChangedEvent.removeListener(onProps);
    });
  });

  return {
    value,
    scaled,
    properties: props,
    setNormalised: (v: number) => state.setNormalisedValue(v),
    dragStart: () => state.sliderDragStarted(),
    dragEnd: () => state.sliderDragEnded(),
  };
}
