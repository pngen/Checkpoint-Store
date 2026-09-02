# Contributing to Checkpoint Store

Thank you for your interest in contributing to Checkpoint Store. This project is
open source, vendor neutral, and governed by the Apache License 2.0.

## Scope

Checkpoint Store is a checkpoint-specific physical storage runtime. It owns one
systems boundary:

> How should checkpoints be physically stored, chunked, deduplicated, replicated,
> tiered, verified, retained, garbage-collected, restored, and recovered so the
> system always knows which checkpoint data exists, which bytes are authoritative,
> which chunks are shared, what is durable, what may be reclaimed, and what must
> be restored first?

It must **not** drift into either adjacent boundary:

- **Checkpoint Fabric** governs coherent checkpoint creation, restore, migration,
  rollback, lineage, fencing, and crash recovery.
- **Storage Fabric** governs placement and lifecycle across generic AI objects and
  storage tiers.

Checkpoint Store specializes checkpoint-specific physical storage semantics and
does not duplicate Checkpoint Fabric or become generic Storage Fabric. Keep
contributions within this boundary.

## Language and standards

- C++20 (use the language features and standard library C++20 gives you).
- Strong types, deterministic behavior, guarded lifecycle transitions, exact
  accounting, bounded parsing, bounded allocations, explicit provenance and
  freshness, generation-fenced authority, corruption-resistant metadata,
  cryptographic content identity, and transactional publication.
- All public headers live under `include/checkpointstore/`.
- Prefer the `checkpointstore` namespace.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The project builds cleanly under `/W4 /WX` on MSVC. Do not introduce warnings.

## Configuration options

```cmake
CHECKPOINTSTORE_BUILD_TESTS          # build the test suite
CHECKPOINTSTORE_BUILD_EXAMPLES       # build the example programs
CHECKPOINTSTORE_BUILD_BENCHMARKS     # build the benchmarks
CHECKPOINTSTORE_ENABLE_SYNTHETIC_BACKENDS  # deterministic synthetic backends
CHECKPOINTSTORE_ENABLE_CUDA_PROOF    # optional CUDA restore-to-device proof
```

CUDA is optional and must remain optional.

## Committing

- Keep commit messages neutral and public-facing.
- Use ordinary engineering subjects, for example:
  - `feat: implement checkpoint manifests`
  - `fix: fence stale restore authority`
  - `test: add recovery and gc coverage`
  - `docs: refine README`
- Do not add unintended Co-authored-by trailers.
- Do not commit build artifacts, generated state, or temporary files.

## Adding tests

Tests should accompany behavior changes. See the `tests/` directory for the
CTest target layout. Tests must run reliably in both Release and Debug.

## Reporting issues

Please provide a minimal reproduction, the platform and toolchain used, and the
expected versus actual behavior.
