# Checkpoint Store

Checkpoint Store is an open-source, vendor-neutral C++20 runtime that makes
checkpoint persistence a first-class storage boundary. It is not a generic blob
store and it is not a checkpointing fabric; it owns exactly one systems
question:

> How should checkpoints be physically stored, chunked, deduplicated, replicated,
> tiered, verified, retained, garbage-collected, restored, and recovered so the
> system always knows which checkpoint data exists, which bytes are authoritative,
> which chunks are shared, what is durable, what may be reclaimed, and what must
> be restored first?

## The systems question

Checkpointing and checkpoint storage are different systems boundaries.

- **Checkpoint Fabric** governs coherent checkpoint creation, restore, migration,
  rollback, lineage, fencing, and crash recovery.
- **Checkpoint Store** governs the *storage substrate* beneath those checkpoint
  objects: chunks, manifests, physical placement, deduplication, retention,
  durability, replica state, integrity, garbage collection, restore ordering, and
  recovery evidence.
- **Storage Fabric** governs placement and lifecycle across generic AI objects and
  storage tiers.

Checkpoint Store specializes checkpoint-specific physical storage semantics. It
does **not** duplicate Checkpoint Fabric, and it does **not** become generic
Storage Fabric.

## What this runtime proves

A checkpoint is no longer an opaque directory or a monolithic file. The runtime
can prove which checkpoint generation exists, which chunks compose it, which
bytes are shared, which replicas are authoritative, what integrity evidence
protects them, what retention policy keeps them alive, what may be safely
garbage-collected, what must be restored first, what became stale after failure
or restart, and which restore or storage mutation is still allowed to become
real.

## Checkpoint identity

Checkpoints are addressed by strong, non-interchangeable identities:
`CheckpointId`, `CheckpointVersionId`, `CheckpointFamilyId`, `ManifestId`,
`ChunkId`, `BlobId`, `ReplicaId`, `PlacementId`, `StorageTierId`,
`StorageBackendId`, `StorageNodeId`, `VolumeId`, `RestoreId`,
`RestorePlanId`, `GcEpochId`, `RetentionPolicyId`, `OwnerId`,
`WorkerId`, `WorkerBootId`, `AttemptId`, `DispatchId`,
`ReservationId`, `ObservationId`.

Generations are strong and explicitly ordered (`CheckpointGeneration`,
`ManifestGeneration`, `ChunkGeneration`, `ReplicaGeneration`,
`PlacementGeneration`, `RestoreGeneration`, `GcGeneration`,
`RetentionGeneration`, `AttemptGeneration`, `DispatchGeneration`,
`ObservationGeneration`, `PolicyGeneration`, and more). Comparisons are
explicit (`precedes`, `follows`, `equal_to`).

**Authority is incarnation-scoped.** A numerically larger stale generation from
an old `WorkerBootId` can never fence a fresh process incarnation. Authority is
always `ScopedAuthority<Generation>` combining a boot identity with a
generation.

## Checkpoint families and generations

A checkpoint belongs to a `CheckpointFamilyId` and carries a monotonically
increasing generation. `make_full_descriptor` derives the next generation for a
family. A publication is only accepted for the *current* generation; a stale
generation is rejected, and a duplicate checkpoint id is always rejected as a
duplicate commit.

## Chunks

Checkpoint Store uses content-addressed fixed-size chunks. The chunker is
deterministic: the same bytes and the same chunk size always produce the same
boundaries. Zero chunk sizes, overflow, gaps, overlaps, duplicate offsets, and
manifest totals that disagree are rejected.

## Manifests

A `CheckpointManifest` records the checkpoint identity, generation, ordered
chunk list, logical size, checkpoint digest, lineage and parent/base references,
durability, and a SHA-256 semantic digest. Chunk order is deterministic and
gap-free (a gap or overlap is rejected). The manifest is canonicalized into a
SHA-256 semantic digest for corruption-resistant identity.

## SHA-256 integrity

SHA-256 is used for chunk content, the full checkpoint content digest, and the
manifest semantic digest. CRC-32 is used for metadata framing (persistence and
protocol) corruption detection. Integrity states are `UNKNOWN`, `UNVERIFIED`,
`VERIFIED`, `CORRUPT`, `MISSING`. `UNKNOWN` is never promoted to
`VERIFIED` without evidence. A corrupt blob is never used to satisfy a restore.

## Deduplication

Deduplication is central. Blob identity is stable across identical content, so a
chunk whose SHA-256 and logical length exactly match an existing verified blob is
reused (a dedup hit) instead of written again. Refcounts are exact; duplicate
release never underflows; a digest collision with disagreeing size is rejected
rather than silently merged. Logical bytes, unique physical bytes, and
deduplicated bytes are tracked.

## Replicas

A `ReplicaSet` tracks a checkpoint's required replica count, authoritative
replicas, health, and placement generation. A checkpoint requiring N replicas is
only durability-satisfied when at least N replicas are verified and
authoritative. States include `UNDER_REPLICATED`, `HEALTHY`, `DEGRADED`,
`REBUILDING`, `FAILED`. `evaluate_replica_durability` makes this explicit.

## Retention

Retention classes include `PINNED`, `KEEP_LATEST_N`, `TTL`,
`ANCESTRY_REQUIRED`, `RESTORE_POINT`, `RECOMPUTABLE`,
`GC_ELIGIBLE`. `KEEP_LATEST_N` operates per `CheckpointFamilyId`.
`ANCESTRY_REQUIRED` protects parents and base checkpoints still required by a
retained descendant. A checkpoint cannot become `GC_ELIGIBLE` if it is pinned,
within the retained latest-N, TTL-unexpired, needed by a retained descendant,
used by an active restore, required for durability/recovery, or protected by
policy. An explicit `retire` overrides automatic latest-N/TTL so the checkpoint
becomes a GC candidate.

## Garbage collection

GC is a safe mark- and sweep. It marks reachable blobs from the set of
protected (retained) checkpoints and recomputes reachability *at delete time*, so
a stale snapshot can never delete a blob that was re-referenced after the plan.
Shared chunks referenced by a retained checkpoint are never reclaimed. Blobs are
removed only when their refcount reaches zero and they are unreachable.
Conservative deferral is used on refcount disagreement rather than guessing.

## Restore planning

Restore is checkpoint-specific and first-class. `plan_restore` builds an ordered
chain (base to derived) and per-chunk steps. `restore` reads a verified replica,
verifies every chunk, assembles the byte stream, and verifies the final
checkpoint digest. Priority classes are `CRITICAL`, `HIGH`, `NORMAL`,
`LOW`, `BACKGROUND`. If one replica is corrupt and another is healthy, the
healthy replica is preferred; a corrupt sole source refuses to restore.

## Restore authority

Restore requests are generation-fenced. A stale boot cannot restore or publish a
fresh result. Retry creates a fresh attempt. On restart, live authority is
cleared and physical observations require revalidation.

## Transactional publication

Publication validates a descriptor, reserves storage, chunks input, computes
digests, performs deduplication, writes new blobs to staging, flushes, verifies
chunks, builds a canonical manifest, verifies the final digest, publishes the
manifest, and commits authority. On failure the checkpoint never becomes
`COMMITTED`, temp data is removed, newly created unreferenced blobs become
reclaimable, existing shared blobs are untouched, and prior committed
checkpoints remain authoritative.

## Local backend

The real local filesystem backend is rooted at a controlled directory and stores
content-addressed blobs under `blobs/<prefix>/<digest>`, staging under
`temp/`, manifests under `manifests/`, and metadata state under
`metadata/checkpoint-store.state`. Keys are backend-relative and governed;
absolute paths, traversal, embedded NUL, and reserved components are rejected.
The backend advertises `LOCAL_FILESYSTEM` tier and reports measured
capacity/free-space with `MEASURED` provenance. Unknown physical-device facts
(path-relative Latency/Throughput, NVMe class) are left `UNKNOWN`.

## Synthetic tiers

Deterministic synthetic backends (provenance `SYNTHETIC`) model unavailable
remote/object/archive tiers so multi-tier, replica-divergence, and capacity
scenarios can be exercised honestly without fabricating cloud hardware. They are
process-local and not durable across restart.

## Multiprocess authority proof

`checkpoint-store-coordinator` and `checkpoint-store-worker` are real OS
processes that communicate over framed TCP. The protocol uses bounded versioned
frames (magic, version, kind, payload length, CRC-32) and rejects bad magic,
unsupported versions, oversized payloads, truncation, checksum mismatch, invalid
enums, and trailing garbage. The coordinator assigns worker boot identities and a
coordinator epoch, and a worker cannot publish or restore under a stale boot or
a stale epoch. A stale replay is rejected; a fresh incarnation can retry under
current authority.

## Persistence and recovery

Logical state is persisted in a versioned binary format with a magic header,
version, bounded counts, deterministic encoding, a CRC-32 frame checksum, and a
SHA-256 semantic digest. Writes are atomic (temp → flush → rename). On recovery,
live worker authority is cleared, active writes become recovery-required/failed,
active restores require retry, stale physical observations become
`REVALIDATION_REQUIRED`, and stale GC snapshots cannot resume destructively.
Corruption, truncation, checksum mismatch, bad magic/version, impossible counts,
duplicate IDs, generation regression, and trailing garbage are all rejected.

## Freshness

`CURRENT`, `STALE`, `REVALIDATION_REQUIRED`, `UNKNOWN` differentiate
logical content identity from physical observation freshness. A SHA-256 identity
can remain stable while measured throughput becomes stale.

## CLI

`checkpoint-store` exposes `family-create`, `checkpoint-create`,
`checkpoint-show`, `publish`, `verify`, `restore`, `manifest`,
`chunks`, `replicas`, `retain`, `retire`, `gc-plan`, `gc-run`,
`explain`, `simulate`, `save`, `recover`, and `benchmark`. Output
exposes generation, digest, manifest, chunk count, logical bytes, physical
bytes, dedup bytes, retention, durability, provenance, freshness, and authority.

## Examples

Approximately fifteen runnable examples live under `examples/`, covering
identity, full checkpoint publication, chunk/manifest, integrity verification,
deduplication, partial-change dedup, replica state, restore, restore priority,
latest-N retention, ancestry retention, GC of shared chunks, persistence and
recovery, multiprocess authority, and the CUDA restore path.

## Benchmarks

A `benchmarks` target measures completed work: SHA-256 chunk hashing, fixed-size
chunking, dedup lookup, manifest publication metadata, restore, protocol
encode/decode, and real local I/O (sequential checkpoint write and read,
reported as `LOCAL_FILESYSTEM` observed throughput).

## Downstream package use

```cmake
find_package(CheckpointStore CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE CheckpointStore::checkpointstore)
```

Installed headers live under `include/checkpointstore/`. CUDA is optional.

## Limitations

- The real local filesystem backend is validated; remote/object/archive tiers
  are synthetic unless physically present.
- No cloud durability class is claimed without a real backend.
- No GPUDirect Storage claim is made; an accelerator proof is an ordinary
  host-to-device copy after a verified CPU restore.
- Local filesystem throughput may include OS caching and is labelled
  `LOCAL_FILESYSTEM` observed throughput, not physical media throughput.
- Incremental/differential semantics are limited to manifest-level parent/base
  relationships; no application-level delta encoding is implemented.
- Unknown physical facts remain `UNKNOWN`.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
