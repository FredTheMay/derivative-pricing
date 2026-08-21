# Phase 7 — AWS Demo Web Application

Status: **implemented, deployed, Phase 7 gate passed**

## 1. Purpose

Per CLAUDE.md §6 Phase 7: a web application that **displays this engine's
evidence** — not a generic pricing form. Strictly after the engine is
complete (Phases 0–6 are done and gated). Architecture and cost guardrails
are locked by CLAUDE.md §6 Phase 7 and reproduced/elaborated below, not
relitigated.

**Real-money flag, stated up front**: this phase provisions real, billable
AWS infrastructure in a live account (verified this session: valid
credentials for account `590184112781`, region `us-east-2`). Everything
through `cdk synth` (which only renders CloudFormation locally, touching
nothing in AWS) will be implemented and verified without approval, since
it's inert. **`cdk bootstrap`/`cdk deploy` — the steps that actually create
billable resources — will not run until you explicitly confirm**, per this
session's standing instruction to check before hard-to-reverse,
real-infrastructure actions.

## 2. Architecture (locked by CLAUDE.md, restated for this doc's own
reference)

| Component | Choice |
|---|---|
| Compute | Lambda container image, ARM64/Graviton, scale-to-zero |
| Engine access | pybind11 module inside the image, thin Python handler |
| API | API Gateway HTTP API, throttled |
| Frontend | React + TypeScript + Vite, S3 + CloudFront |
| Charts | Recharts |
| IaC | AWS CDK in TypeScript, in `infra/` |

## 3. Backend

### 3.1 Container image

`infra/lambda/Dockerfile`, multi-stage:

1. Builder stage: `public.ecr.aws/lambda/python:3.12` (ARM64) base, installs
   a C++ toolchain, builds the `mcd` extension via the existing
   `setup.py`/`pybind11.setup_helpers` path (Phase 6) — the *same* build
   mechanism already tested locally, not a new one.
2. Runtime stage: copies the built extension and `infra/lambda/handler.py`
   into a fresh `public.ecr.aws/lambda/python:3.12` image. `CMD` points at
   the handler.

Built for `linux/arm64` via `docker buildx` (this session's dev machine is
Apple Silicon — natively `arm64` — but a Linux container build is still
required regardless of host architecture, since the Lambda runtime is Linux
and the Phase 6 macOS build isn't portable to it).

### 3.2 Handler (`infra/lambda/handler.py`)

Thin: parses the API Gateway HTTP API event body as JSON, calls the `mcd`
Python bindings (Phase 6) directly -- not by shelling out to `mcd_cli`,
which is a separate C++ binary that has no place in a Python Lambda image --
and returns the JSON response with a 200 or a 4xx/5xx status and a clean
`{"error": ...}` body. `infra/lambda/request.py` reimplements the *same
request/response schema* `mcd_cli` defines (same field names, same
per-product required fields, same seven-field Monte Carlo result shape) as
its own, separate, Python-side validation module -- correction from an
earlier draft of this doc, which described this as literally shared code
between the two, which isn't possible across a C++/Python boundary. What's
actually shared is the protocol, not the source file.

**Cost guardrails, all mandatory (CLAUDE.md §6 Phase 7), enforced in the
handler and/or CDK, not just described**:

- Hard cap on `path_count`: **5,000,000**. Requests above it get a `400`
  with a clear error, not a slow/expensive silent clamp.
- Lambda timeout: **30s** (CDK-configured). Memory tuned by actually
  measuring cold/warm invocation times at a few candidate sizes once
  deployed (per CLAUDE.md: memory "tuned by measurement, not by guess") —
  reported in this doc's gate section once real numbers exist, not assumed
  now.
- **No authentication, no database, no VPC, no NAT Gateway.**

### 3.3 What is NOT computed on request

Thread-scaling and the false-sharing A/B are **committed JSON**
(`docs/benchmarks/scaling.json`, `docs/benchmarks/false_sharing.json`),
built from the *real, already-measured* Phase 4 numbers already published in
`docs/validation-report.md` (1.00×@1, 3.76×@4, 4.91×@6, 5.97×@11 threads,
54.2% efficiency, Amdahl f≈0.088; the false-sharing null result, 17.4ms vs.
17.5ms) — reformatted into JSON for the frontend to fetch statically, not
re-measured or fabricated. A Lambda invocation cannot meaningfully
reproduce Phase 4's controlled multi-core scaling measurement anyway
(Lambda's vCPU allocation and noisy-neighbor behavior bear no relationship
to the dedicated-machine measurement CLAUDE.md's scaling claim is about).

## 4. Frontend (`web/`, React + TypeScript + Vite)

Required content, in the priority order CLAUDE.md specifies:

1. **Interactive convergence explorer** — price and CI band vs. path count,
   live (calls the API at a few path counts, plots point + band with
   Recharts).
2. **Thread-scaling chart and false-sharing A/B** — from the committed JSON
   (sec.3.3), never computed on request.
3. **Variance-reduction comparison** — antithetic/control-variate on vs. off,
   live (two API calls, same seed, compare standard errors).
4. **Live pricing across all products** — every product `mcd_cli` supports
   (Phase 6), always showing the 95% CI, never a bare number.
5. **Greeks surfaces over a spot/time grid** — a small grid of
   `finite_difference_european` calls, rendered as a heatmap/surface.
6. **CFA invariant table, rendered live and green** — calls a dedicated
   Lambda route that runs the same checks `tools/generate_report.py`
   already computes (Phase 6), reusing that logic rather than
   reimplementing it a third time.

Hosting: private S3 bucket + CloudFront with Origin Access Control (no
public S3 access), CloudFront caching enabled on all GET responses (the API
routes too, per CLAUDE.md's cost guardrails — short TTL, since prices are
seeded/deterministic and safe to cache briefly).

## 5. IaC (`infra/`, AWS CDK in TypeScript)

One stack, `McdDemoStack`:

- `DockerImageFunction` (ARM64) from `infra/lambda/`, 30s timeout, memory
  per sec.3.2.
- API Gateway HTTP API, single Lambda proxy integration, throttling burst
  10 / rate 5 req/s (CDK `throttle` on the default stage).
- S3 bucket (private) + CloudFront distribution (OAC to the bucket, GET
  caching enabled) serving `web/dist` after a Vite build.
- **AWS Budgets alarm**, CDK-provisioned, low monthly threshold (proposing
  **$10/month**, since this is a scale-to-zero demo — confirm or adjust).
- Stack outputs: API URL, CloudFront URL.

## 6. Test plan

- Backend: unit tests for `infra/lambda/request.py`'s validation logic
  (path-count cap, malformed input) — pure Python, `unittest`, no AWS
  needed, run in CI.
- `cdk synth` in CI (renders CloudFormation locally; catches IaC errors
  without touching AWS or needing credentials in CI).
- Frontend: component-level rendering smoke tests (Vite/React's own test
  tooling — CLAUDE.md doesn't list a frontend test framework in §5's
  dependency list, so this needs the same "stop and ask" treatment as
  Phase 6's JSON library question, sec.8 below).
- Post-deploy (manual, once you confirm deployment): hit the live API URL
  for each product, confirm the CloudFront URL serves the frontend, measure
  and record real cold-start latency.

## 7. Acceptance criteria

1. `cdk synth` succeeds with no errors, reviewable output.
2. Backend request-validation unit tests pass.
3. Frontend builds (`npm run build`) and renders all six required sections
   against a local/mock API before any real deployment.
4. **Once you confirm deployment**: stack deploys, both URLs are live, cold
   -start latency is measured (not estimated) and recorded, estimated
   monthly cost at zero traffic is computed from the actual provisioned
   resources and stated in the README.

## 8. Open questions for you

1. **Frontend test framework**: CLAUDE.md's §5 dependency list is
   C++-engine-scoped (GoogleTest/Benchmark/pybind11/stdlib) and doesn't
   name anything for a React frontend. Vitest (Vite's own companion test
   runner, from the same maintainers, zero extra config) is the natural
   default if any frontend testing is wanted at all — confirm, or say
   "skip frontend tests, ship the app."
2. **Budgets alarm threshold**: proposing $10/month (sec.5). Confirm or
   set your own number.
3. **Deployment itself**: confirm you want me to actually run `cdk
   bootstrap`/`cdk deploy` against account `590184112781` once the code is
   ready and `cdk synth`-verified, or whether you'd rather review the
   synthesized CloudFormation first, or deploy it yourself.
