// The juce-framework-frontend package is plain JS (no bundled types).
// This file declares just the surface we use.
declare module "juce-framework-frontend" {
  export interface SliderProperties {
    start: number;
    end: number;
    skew: number;
    name: string;
    label: string;
    numSteps: number;
    interval: number;
    parameterIndex: number;
  }

  export interface ListenerHandle {
    readonly __brand: unique symbol;
  }

  export interface EventEmitter {
    addListener(fn: () => void): number;
    removeListener(id: number): void;
  }

  export interface SliderState {
    readonly name: string;
    readonly properties: SliderProperties;
    readonly valueChangedEvent: EventEmitter;
    readonly propertiesChangedEvent: EventEmitter;
    getScaledValue(): number;
    getNormalisedValue(): number;
    setNormalisedValue(value: number): void;
    sliderDragStarted(): void;
    sliderDragEnded(): void;
  }

  export interface ToggleProperties {
    name: string;
    parameterIndex: number;
  }

  export interface ToggleState {
    readonly name: string;
    readonly properties: ToggleProperties;
    readonly valueChangedEvent: EventEmitter;
    readonly propertiesChangedEvent: EventEmitter;
    getValue(): boolean;
    setValue(value: boolean): void;
  }

  export function getSliderState(name: string): SliderState;
  export function getToggleState(name: string): ToggleState;
  export function getNativeFunction<
    TArgs extends unknown[] = unknown[],
    TResult = unknown,
  >(name: string): (...args: TArgs) => Promise<TResult>;
  export function getBackendResourceAddress(path: string): string;
}
