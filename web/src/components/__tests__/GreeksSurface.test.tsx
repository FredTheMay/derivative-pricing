import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { GreeksSurface } from "../GreeksSurface";

describe("GreeksSurface", () => {
  it("computes a 5x5 grid of Greeks and renders it as a table", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        delta: 0.5, gamma: 0.02, vega: 30, theta: -5, rho: 40, elapsed_seconds: 0.1,
      }),
    }));
    render(<GreeksSurface />);
    fireEvent.click(screen.getByRole("button", { name: /compute surface/i }));
    await waitFor(() => expect(fetch).toHaveBeenCalledTimes(25), { timeout: 3000 });
    // 5x5 grid + header row/column -> 25 data cells with the mocked delta value.
    const cells = screen.getAllByText("0.500");
    expect(cells.length).toBe(25);
  });
});
