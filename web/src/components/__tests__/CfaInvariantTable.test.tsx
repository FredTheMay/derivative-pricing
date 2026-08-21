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
  });
});
