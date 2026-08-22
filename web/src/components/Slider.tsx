import { useId } from "react";

interface SliderProps {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  onChange: (value: number) => void;
  /** Formats the live readout next to the label -- defaults to the raw value. */
  format?: (value: number) => string;
}

// No range-input precedent exists elsewhere in this codebase (every other control is a
// <select> feeding a button-triggered fetch) -- this is the one new interaction primitive
// the CFA education dashboard needs: instant client-side recompute on drag, no button, no
// network call. Reuses --accent/--surface-2/--border from index.css so it matches the
// existing dark theme exactly.
export function Slider({ label, value, min, max, step, onChange, format }: SliderProps) {
  const id = useId();
  const display = format ? format(value) : String(value);

  return (
    <div className="slider-field">
      <div className="slider-label-row">
        <label htmlFor={id}>{label}</label>
        <span className="slider-value">{display}</span>
      </div>
      <input
        id={id}
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
      />
    </div>
  );
}
