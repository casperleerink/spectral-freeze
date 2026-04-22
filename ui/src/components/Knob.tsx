import { createMemo, type Component } from "solid-js";
import { useSlider } from "../hooks/useSlider";

// Arc-drawn rotary knob. Vertical drag maps pixels → normalised value.
// 1 pixel = 0.4% by default; hold Shift for fine (0.05%).
type Props = {
  paramId: string;
  label: string;
  size?: number;
  format?: (scaled: number) => string;
};

const MIN_ANGLE = -135; // degrees
const MAX_ANGLE = 135;

export const Knob: Component<Props> = (props) => {
  const slider = useSlider(props.paramId);
  const size = () => props.size ?? 110;

  const angle = createMemo(
    () => MIN_ANGLE + slider.value() * (MAX_ANGLE - MIN_ANGLE),
  );

  let startY = 0;
  let startValue = 0;
  let dragging = false;

  const onPointerDown = (e: PointerEvent) => {
    e.preventDefault();
    (e.target as Element).setPointerCapture(e.pointerId);
    startY = e.clientY;
    startValue = slider.value();
    dragging = true;
    slider.dragStart();
  };

  const onPointerMove = (e: PointerEvent) => {
    if (!dragging) return;
    const sensitivity = e.shiftKey ? 0.0005 : 0.004;
    const dy = startY - e.clientY;
    const next = Math.min(1, Math.max(0, startValue + dy * sensitivity));
    slider.setNormalised(next);
  };

  const onPointerUp = (e: PointerEvent) => {
    if (!dragging) return;
    dragging = false;
    (e.target as Element).releasePointerCapture(e.pointerId);
    slider.dragEnd();
  };

  const onDoubleClick = () => {
    // Reset to default not exposed via relay; center-ish fallback:
    slider.dragStart();
    slider.setNormalised(0);
    slider.dragEnd();
  };

  const display = () => {
    const fmt = props.format;
    const s = slider.scaled();
    return fmt ? fmt(s) : s.toFixed(2);
  };

  // SVG arc geometry — foreground sweep from MIN_ANGLE → current angle.
  const radius = () => size() / 2 - 8;
  const cx = () => size() / 2;
  const cy = () => size() / 2;

  const arcPath = createMemo(() => {
    const a0 = (MIN_ANGLE - 90) * (Math.PI / 180);
    const a1 = (angle() - 90) * (Math.PI / 180);
    const r = radius();
    const x0 = cx() + r * Math.cos(a0);
    const y0 = cy() + r * Math.sin(a0);
    const x1 = cx() + r * Math.cos(a1);
    const y1 = cy() + r * Math.sin(a1);
    const large = angle() - MIN_ANGLE > 180 ? 1 : 0;
    return `M ${x0} ${y0} A ${r} ${r} 0 ${large} 1 ${x1} ${y1}`;
  });

  const trackPath = createMemo(() => {
    const a0 = (MIN_ANGLE - 90) * (Math.PI / 180);
    const a1 = (MAX_ANGLE - 90) * (Math.PI / 180);
    const r = radius();
    const x0 = cx() + r * Math.cos(a0);
    const y0 = cy() + r * Math.sin(a0);
    const x1 = cx() + r * Math.cos(a1);
    const y1 = cy() + r * Math.sin(a1);
    return `M ${x0} ${y0} A ${r} ${r} 0 1 1 ${x1} ${y1}`;
  });

  return (
    <div class="flex flex-col items-center gap-2 select-none">
      <div class="text-xs uppercase tracking-widest text-[color:var(--color-fg-dim)]">
        {props.label}
      </div>
      <div
        class="relative cursor-ns-resize touch-none"
        style={{ width: `${size()}px`, height: `${size()}px` }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        onDblClick={onDoubleClick}
      >
        <svg width={size()} height={size()} class="absolute inset-0">
          <path
            d={trackPath()}
            fill="none"
            stroke="var(--color-panel-border)"
            stroke-width="4"
            stroke-linecap="round"
          />
          <path
            d={arcPath()}
            fill="none"
            stroke="var(--color-accent)"
            stroke-width="4"
            stroke-linecap="round"
          />
          <g transform={`rotate(${angle()} ${cx()} ${cy()})`}>
            <circle
              cx={cx()}
              cy={cy()}
              r={radius() - 12}
              fill="var(--color-panel-2)"
              stroke="var(--color-panel-border)"
              stroke-width="1"
            />
            <line
              x1={cx()}
              y1={cy() - radius() + 14}
              x2={cx()}
              y2={cy() - radius() + 24}
              stroke="var(--color-accent)"
              stroke-width="2"
              stroke-linecap="round"
            />
          </g>
        </svg>
      </div>
      <div class="font-mono text-sm text-[color:var(--color-fg)]">
        {display()}
      </div>
    </div>
  );
};
