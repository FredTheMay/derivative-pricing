import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { RatesFraLab } from "../RatesFraLab";

describe("RatesFraLab", () => {
  it("renders the default yield curve with discount factors", () => {
    render(<RatesFraLab />);
    expect(screen.getByText("Interest Rates, FRAs & Futures")).toBeInTheDocument();
    // DF for period 1 at spot rate 2.5% is 1/1.025 = 0.975610.
    expect(screen.getByText("0.975610")).toBeInTheDocument();
  });

  it("computes the implied forward rate between two selected curve periods", () => {
    render(<RatesFraLab />);
    const selectA = screen.getByLabelText("Period A");
    const selectB = screen.getByLabelText("Period B");
    fireEvent.change(selectA, { target: { value: "0.5" } });
    fireEvent.change(selectB, { target: { value: "1" } });
    // (1.025^1 / 1.02^0.5)^(1/0.5) - 1 ~= 3.002%.
    expect(screen.getByText("3.002%")).toBeInTheDocument();
  });

  it("shows a placeholder instead of a rate when B is not after A", () => {
    render(<RatesFraLab />);
    const selectA = screen.getByLabelText("Period A");
    const selectB = screen.getByLabelText("Period B");
    fireEvent.change(selectA, { target: { value: "1.5" } });
    fireEvent.change(selectB, { target: { value: "1" } });
    expect(screen.getByText("Pick B > A")).toBeInTheDocument();
  });

  it("adds a new curve row when 'Add period' is clicked", () => {
    render(<RatesFraLab />);
    const rowsBefore = screen.getAllByRole("button", { name: "Remove" }).length;
    fireEvent.click(screen.getByRole("button", { name: /add period/i }));
    const rowsAfter = screen.getAllByRole("button", { name: "Remove" }).length;
    expect(rowsAfter).toBe(rowsBefore + 1);
  });
});
