import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { GreeksSurface } from "../GreeksSurface";

describe("GreeksSurface", () => {
  it("defaults to likelihood-ratio gamma and renders a 5x5 grid", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        delta: 0.5, delta_se: 0.01, gamma: 0.02, gamma_se: 0.001, vega: 30, vega_se: 0.5,
        rho: 40, rho_se: 0.2, theta: -5, theta_se: 0.1, elapsed_seconds: 0.1,
      }),
    }));
    render(<GreeksSurface />);
    fireEvent.click(screen.getByRole("button", { name: /compute surface/i }));
    await waitFor(() => expect(fetch).toHaveBeenCalledTimes(25), { timeout: 3000 });
    const bodies = (fetch as ReturnType<typeof vi.fn>).mock.calls.map(
      (c) => JSON.parse((c[1] as RequestInit).body as string),
    );
    expect(bodies[0].request).toBe("lr_greeks");
    // 5x5 grid + header row/column -> 25 data cells with the mocked gamma value.
    const cells = screen.getAllByText("0.020");
    expect(cells.length).toBe(25);
  });

  it("switches to finite-difference when selected", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ delta: 0.5, gamma: 0.02, vega: 30, theta: -5, rho: 40,
                            elapsed_seconds: 0.1 }),
    }));
    render(<GreeksSurface />);
    const selects = screen.getAllByRole("combobox");
    fireEvent.change(selects[1], { target: { value: "fd" } });
    fireEvent.click(screen.getByRole("button", { name: /compute surface/i }));
    await waitFor(() => expect(fetch).toHaveBeenCalledTimes(25), { timeout: 3000 });
    const bodies = (fetch as ReturnType<typeof vi.fn>).mock.calls.map(
      (c) => JSON.parse((c[1] as RequestInit).body as string),
    );
    expect(bodies[0].request).toBe("greeks");
  });
});
