# Multi-stage build for the mcd demo Lambda, ARM64/Graviton (CLAUDE.md sec.6 Phase 7).
#
# Builder stage compiles the `mcd` pybind11 extension via the *same* setup.py/
# pybind11.setup_helpers path already built and tested locally in Phase 6 -- not a new
# build mechanism, just run inside a Linux/ARM64 container instead of on the macOS dev
# machine, since the Lambda runtime is Linux and Phase 6's local .so isn't portable to it.
#
# Build with: docker buildx build --platform linux/arm64 -t mcd-lambda infra/lambda \
#   -f infra/lambda/Dockerfile <repo-root>
# (build context must be the repo root, not infra/lambda/, since the builder stage needs
# include/ and src/ from the main engine tree.)

FROM public.ecr.aws/lambda/python:3.12 AS builder

RUN dnf install -y gcc-c++ && dnf clean all
RUN pip install --no-cache-dir "pybind11>=2.10" "setuptools>=64"

WORKDIR /build
COPY include/ include/
COPY src/core/types.cpp src/core/normal.cpp src/core/rng.cpp src/core/thread_pool.cpp \
     src/core/linalg.cpp src/core/sobol.cpp src/core/rng_simd.cpp \
     src/core/gauss_legendre.cpp src/core/
COPY src/models/ src/models/
COPY src/pricers/ src/pricers/
COPY src/greeks/ src/greeks/
COPY bindings/python/src/module.cpp bindings/python/src/
COPY setup.py pyproject.toml ./

RUN python setup.py build_ext --inplace

FROM public.ecr.aws/lambda/python:3.12

COPY --from=builder /build/mcd*.so ${LAMBDA_TASK_ROOT}/
COPY infra/lambda/handler.py infra/lambda/request.py infra/lambda/cfa_invariants.py \
     ${LAMBDA_TASK_ROOT}/

CMD ["handler.lambda_handler"]
