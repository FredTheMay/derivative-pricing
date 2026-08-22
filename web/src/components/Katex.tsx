import { useMemo } from "react";
import katex from "katex";

// Every formula rendered through this component is a developer-authored constant (see
// web/src/lib/financeFormulas.ts) -- never user input -- so a parse failure can only mean
// a typo, not a runtime condition to silently tolerate. throwOnError: true + a catch here
// (rather than KaTeX's own inline error styling) keeps a broken formula visually
// consistent with every other error state in this app (the shared .error class), and
// console.error keeps it visible in dev tooling. financeFormulas.test.ts additionally
// asserts every exported LaTeX constant parses cleanly, so this catch path is a defense
// in depth, not the primary way a typo gets caught.
export function Katex({ latex, display = true }: { latex: string; display?: boolean }) {
  const html = useMemo(() => {
    try {
      return katex.renderToString(latex, { throwOnError: true, displayMode: display });
    } catch (e) {
      console.error(`KaTeX failed to render "${latex}":`, e);
      return null;
    }
  }, [latex, display]);

  if (html === null) {
    return <p className="error">Formula failed to render: <code>{latex}</code></p>;
  }
  // eslint-disable-next-line react/no-danger -- katex.renderToString's own output, not
  // user-supplied HTML.
  return <span className={display ? "katex-block" : undefined} dangerouslySetInnerHTML={{ __html: html }} />;
}
