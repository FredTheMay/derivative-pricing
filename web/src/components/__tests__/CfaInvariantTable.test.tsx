import { render, screen, waitFor } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { CfaInvariantTable } from "../CfaInvariantTable";

describe("CfaInvariantTable", () => {
  it("renders all-pass state when every invariant passes", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        invariants: [
          { module: "LM4", invariant: "Cost of carry", pass: true, deviation: 0 },
          { module: "LM9", invariant: "Put-call parity", pass: true, deviation: 1e-14 },
        ],
      }),
    }));
    render(<CfaInvariantTable />);
    await waitFor(() => expect(screen.getByText("All invariants pass.")).toBeInTheDocument());
    expect(screen.getAllByText("PASS")).toHaveLength(2);
    // Summary roll-up: 2 total, 2 passed, 0 failed.
    expect(screen.getByText("Total invariants").closest(".stat")?.querySelector(".value")?.textContent).toBe("2");
    expect(screen.getByText("Passed").closest(".stat")?.querySelector(".value")?.textContent).toBe("2");
    expect(screen.getByText("Failed").closest(".stat")?.querySelector(".value")?.textContent).toBe("0");
  });

  it("flags a failure clearly rather than hiding it", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        invariants: [
          { module: "LM9", invariant: "Put-call parity", pass: false, deviation: 0.5 },
        ],
      }),
    }));
    render(<CfaInvariantTable />);
    await waitFor(() => expect(screen.getByText(/some invariants failed/i)).toBeInTheDocument());
    expect(screen.getByText("FAIL")).toBeInTheDocument();
    expect(screen.getByText("Failed").closest(".stat")?.querySelector(".value")?.textContent).toBe("1");
  });
});
