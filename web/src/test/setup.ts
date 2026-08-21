import "@testing-library/jest-dom/vitest";

// jsdom doesn't implement ResizeObserver, which Recharts' ResponsiveContainer needs.
// Real usage only requires "doesn't throw" in a test environment -- the actual resize
// behavior is exercised by a real browser, not this stub.
class ResizeObserverStub {
  observe() {}
  unobserve() {}
  disconnect() {}
}
globalThis.ResizeObserver = ResizeObserverStub;
