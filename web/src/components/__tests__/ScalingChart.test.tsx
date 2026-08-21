import { render, screen, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { ScalingChart } from "../ScalingChart";

describe("ScalingChart", () => {
  beforeEach(() => {
    vi.stubGlobal("fetch", vi.fn((url: string) => {
      if (url.includes("scaling")) {
        return Promise.resolve({
          ok: true,
          json: async () => ({
            hardware: "Apple M3 Pro", amdahl_serial_fraction: 0.088,
            points: [{ threads: 1, speedup: 1, efficiency: 1 }],
          }),
        });
      }
      return Promise.resolve({
        ok: true,
        json: async () => ({
          result: "no measurable difference",
          layouts: [{ layout: "unpadded", median_ms: 17.4 }, { layout: "padded", median_ms: 17.5 }],
        }),
      });
    }));
  });

  it("fetches and renders the precomputed benchmark JSON, never computing on request", async () => {
    render(<ScalingChart />);
    await waitFor(() => expect(screen.getByText(/Apple M3 Pro/)).toBeInTheDocument());
    expect(fetch).toHaveBeenCalledWith("/benchmarks/scaling.json");
    expect(fetch).toHaveBeenCalledWith("/benchmarks/false_sharing.json");
    expect(screen.getByText(/no measurable difference/)).toBeInTheDocument();
  });
});
