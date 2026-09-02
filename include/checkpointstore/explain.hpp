#ifndef CHECKPOINTSTORE_EXPLAIN_HPP
#define CHECKPOINTSTORE_EXPLAIN_HPP

#include <checkpointstore/model.hpp>
#include <checkpointstore/storage/dedup.hpp>

#include <string>

namespace checkpointstore {

// Human-readable explanation strings. These are deterministic and intended for
// operators and for the explain CLI, not for control flow.
[[nodiscard]] std::string explain_checkpoint(const CheckpointDescriptor& d);
[[nodiscard]] std::string explain_manifest(const CheckpointManifest& m);
[[nodiscard]] std::string explain_dedup(const DedupEntry& e, bool reused);
[[nodiscard]] std::string explain_replica(const ReplicaDescriptor& r);
[[nodiscard]] std::string explain_restore(const RestoreEvidence& ev);
[[nodiscard]] std::string explain_retention(const RetentionPolicy& p, bool protected_);
[[nodiscard]] std::string explain_gc(const GcState& s);
[[nodiscard]] std::string explain_integrity(IntegrityState state);

}  // namespace checkpointstore

#endif
