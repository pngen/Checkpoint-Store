#include <checkpointstore/store.hpp>

#include "binformat.hpp"
#include "impl.hpp"
#include "persistence.hpp"

#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/storage/chunking.hpp>
#include <checkpointstore/storage/dedup.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/storage/synthetic_backend.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace checkpointstore {

namespace {

// 2-character prefix directory for content-addressed blobs.
std::string blob_prefix(const crypto::Sha256Digest& d) {
    return crypto::hex(d).substr(0, 2);
}

// Governed backend key for a blob.
BackendKey blob_key(const crypto::Sha256Digest& d) {
    return BackendKey{"blobs/" + blob_prefix(d) + "/" + crypto::hex(d)};
}

// Canonical manifest encoding for the semantic digest.
crypto::Sha256Digest manifest_semantic_digest(const CheckpointManifest& m) {
    BinWriter w;
    w.id(m.id);
    w.generation(m.generation);
    w.id(m.checkpoint_id);
    w.generation(m.checkpoint_generation);
    w.u32(static_cast<std::uint32_t>(m.chunks.size()));
    for (const auto& c : m.chunks) {
        w.id(c.chunk_id);
        w.u64(c.logical_offset);
    }
    w.u64(m.logical_size);
    w.sha256(m.checkpoint_digest);
    w.u8(static_cast<std::uint8_t>(m.provenance));
    w.u8(static_cast<std::uint8_t>(m.durability));
    w.u64(m.required_replica_count);
    return crypto::sha256(ByteView(w.data().data(), w.data().size()));
}

}  // namespace

CheckpointStore::CheckpointStore(StoreOptions options) : options_(std::move(options)) {
    impl_ = std::make_unique<Impl>();
    impl_->primary_backend_id = options_.primary_backend_id;
}

CheckpointStore::~CheckpointStore() = default;
CheckpointStore::CheckpointStore(CheckpointStore&&) noexcept = default;
CheckpointStore& CheckpointStore::operator=(CheckpointStore&&) noexcept = default;

void CheckpointStore::register_backend(StorageBackendId id, BackendPtr backend) {
    if (!backend) {
        throw_error(ErrorCode::kInvalidArgument, "null backend");
    }
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->backends[id] = std::move(backend);
}

BackendPtr CheckpointStore::backend(StorageBackendId id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->backends.find(id);
    if (it == impl_->backends.end()) {
        throw_error(ErrorCode::kNotFound, "backend not registered");
    }
    return it->second;
}

std::vector<BackendDescriptor> CheckpointStore::backend_descriptors() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<BackendDescriptor> out;
    for (const auto& [id, b] : impl_->backends) {
        (void)id;
        out.push_back(b->descriptor());
    }
    return out;
}

CheckpointFamilyId CheckpointStore::create_family(OwnerId owner, Provenance provenance) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    const auto id = CheckpointFamilyId(impl_->next_family_id.value());
    impl_->next_family_id = CheckpointFamilyId(impl_->next_family_id.value() + 1);
    Impl::FamilyRecord f;
    f.id = id;
    f.family_generation = CheckpointFamilyGeneration(1);
    impl_->families.emplace(id, f);
    impl_->next_checkpoint_generation[id] = CheckpointGeneration(1);
    (void)owner;
    (void)provenance;
    impl_->counters.family_count = static_cast<std::uint64_t>(impl_->families.size());
    return id;
}

bool CheckpointStore::family_exists(CheckpointFamilyId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->families.count(id) != 0;
}

CheckpointDescriptor CheckpointStore::make_full_descriptor(CheckpointFamilyId family, OwnerId owner,
                                                          std::uint64_t logical_size,
                                                          DurabilityClass durability) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->next_checkpoint_generation.find(family);
    if (it == impl_->next_checkpoint_generation.end()) {
        throw_error(ErrorCode::kNotFound, "family not found");
    }
    CheckpointDescriptor d;
    d.id = CheckpointId(impl_->next_id_counter++);
    d.family_id = family;
    d.generation = it->second;
    d.kind = CheckpointKind::kFull;
    d.logical_size = logical_size;
    d.created_at = std::chrono::system_clock::now();
    d.owner_id = owner;
    d.producer_boot = options_.boot_id;
    d.producer_generation = it->second;
    d.retention = RetentionClass::kKeepLatestN;
    d.durability = durability;
    d.restore_priority = RestorePriority::kNormal;
    d.provenance = Provenance::kMeasured;
    d.policy_generation = PolicyGeneration(1);
    d.required_replica_count = options_.required_replica_count;
    return d;
}

// --------------------------------------------------------------------------
// Transactional publication.
// --------------------------------------------------------------------------
PublishedCheckpoint CheckpointStore::publish(const CheckpointDescriptor& descriptor, ByteView data) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (descriptor.producer_boot != options_.boot_id) {
        ++impl_->counters.stale_rejections;
        throw_error(ErrorCode::kStaleAuthority, "publish authority is stale");
    }
    // Duplicate commit rejection must fire before generation comparison so a
    // replay of an already-committed checkpoint id is always rejected.
    if (impl_->checkpoints.count(descriptor.id)) {
        ++impl_->counters.duplicate_rejections;
        throw_error(ErrorCode::kAlreadyExists, "checkpoint already committed");
    }
    const auto gen_it = impl_->next_checkpoint_generation.find(descriptor.family_id);
    if (gen_it == impl_->next_checkpoint_generation.end()) {
        throw_error(ErrorCode::kNotFound, "family not found");
    }
    if (!descriptor.generation.equal_to(gen_it->second)) {
        ++impl_->counters.stale_rejections;
        throw_error(ErrorCode::kStaleGeneration, "publish generation is stale");
    }
    if (!FixedChunker::valid_chunk_size(options_.chunk_size)) {
        throw_error(ErrorCode::kInvalidArgument, "invalid configured chunk size");
    }
    if (descriptor.kind != CheckpointKind::kFull && descriptor.kind != CheckpointKind::kSynthetic) {
        throw_error(ErrorCode::kNotSupported,
                    "only full/synthetic checkpoint publication is supported in 1.0.0");
    }
    auto primary_it = impl_->backends.find(impl_->primary_backend_id);
    if (primary_it == impl_->backends.end()) {
        throw_error(ErrorCode::kNotFound, "primary backend not registered");
    }
    IBackend& underlay = *primary_it->second;

    std::vector<ChunkSlice> slices = chunk_stream(data, options_.chunk_size);
    PublishedCheckpoint result;
    result.descriptor = descriptor;

    const ReplicaId replica_id = ReplicaId(impl_->next_id_counter++);
    std::vector<ChunkDescriptor> chunk_descriptors;
    std::vector<ManifestEntry> manifest_entries;
    std::map<ChunkId, std::uint64_t> chunk_sizes;
    std::vector<BackendKey> new_blob_keys;
    std::vector<crypto::Sha256Digest> new_digests;
    std::uint64_t write_size = 0;

    try {
        for (const auto& slice : slices) {
            const auto blob_id = blob_id_from_digest(slice.digest);
            const auto chunk_id = chunk_id_from_digest(slice.digest);
            const BackendKey bkey = blob_key(slice.digest);

            ChunkDescriptor cd;
            cd.id = chunk_id;
            cd.generation = ChunkGeneration(1);
            cd.logical_offset = slice.range.logical_offset;
            cd.logical_size = slice.range.logical_size;
            cd.physical_size = slice.bytes.size();
            cd.digest = slice.digest;
            cd.blob_id = blob_id;
            cd.provenance = Provenance::kMeasured;
            cd.integrity = IntegrityState::kUnverified;
            cd.payload_encoding = "identity";

            bool reused = false;
            const DedupLookup lookup = dedup_.lookup(slice.digest, slice.bytes.size());
            if (lookup.result == DedupResult::kHit) {
                const DedupEntry* e = dedup_.find(slice.digest);
                const IntegrityState exist = underlay.verify(bkey, slice.digest);
                if (exist == IntegrityState::kVerified && e != nullptr) {
                    (void)dedup_.add_reference(slice.digest);
                    reused = true;
                    cd.integrity = IntegrityState::kVerified;
                    cd.blob_id = e->blob_id;
                    ++result.dedup_hits;
                } else {
                    if (exist == IntegrityState::kCorrupt) {
                        ++impl_->counters.integrity_failures;
                    }
                    reused = false;
                }
            }

            if (!reused) {
                underlay.put_temp(bkey, ByteView(slice.bytes.data(), slice.bytes.size()));
                underlay.commit(bkey);
                (void)dedup_.insert(blob_id, slice.digest, slice.bytes.size(),
                                    IntegrityState::kVerified, BlobGeneration(1));
                cd.blob_id = blob_id;
                cd.integrity = IntegrityState::kVerified;
                ++result.dedup_misses;
                write_size += slice.bytes.size();
                new_blob_keys.push_back(bkey);
                new_digests.push_back(slice.digest);
            }

            auto bit = impl_->blobs.find(cd.blob_id);
            if (bit == impl_->blobs.end()) {
                BlobDescriptor bd;
                bd.id = cd.blob_id;
                bd.generation = BlobGeneration(1);
                bd.physical_size = cd.physical_size;
                bd.digest = cd.digest;
                bd.refcount = dedup_.refcount(slice.digest);
                bd.tier = StorageTierClass::kLocalFilesystem;
                bd.provenance = Provenance::kMeasured;
                bd.integrity = cd.integrity;
                bd.replicas.push_back(replica_id);
                impl_->blobs.emplace(bd.id, bd);
            } else {
                if (std::find(bit->second.replicas.begin(), bit->second.replicas.end(),
                              replica_id) == bit->second.replicas.end()) {
                    bit->second.replicas.push_back(replica_id);
                }
            }

            PlacementDescriptor pd;
            pd.id = PlacementId(impl_->next_id_counter++);
            pd.generation = PlacementGeneration(1);
            pd.replica_id = replica_id;
            pd.tier_id = StorageTierId(1);
            pd.backend_id = impl_->primary_backend_id;
            pd.node_id = StorageNodeId(1);
            pd.volume_id = VolumeId(1);
            pd.bytes = cd.physical_size;
            pd.provenance = Provenance::kMeasured;
            pd.freshness = Freshness::kCurrent;
            impl_->placements.emplace(pd.id, pd);
            cd.placements.push_back(pd.id);

            chunk_descriptors.push_back(cd);
            chunk_sizes.emplace(chunk_id, slice.bytes.size());
            manifest_entries.push_back(ManifestEntry{chunk_id, slice.range.logical_offset});
        }

        // Verify all required chunks by reading back from the backend.
        crypto::Sha256Hasher combined;
        for (const auto& cd : chunk_descriptors) {
            const BackendKey key = blob_key(cd.digest);
            Bytes rb = underlay.read(key);
            if (rb.size() != cd.physical_size) {
                ++impl_->counters.integrity_failures;
                throw_error(ErrorCode::kIntegrity, "chunk size mismatch during verify");
            }
            auto got = crypto::sha256(ByteView(rb.data(), rb.size()));
            if (!crypto::equal(got, cd.digest)) {
                ++impl_->counters.integrity_failures;
                throw_error(ErrorCode::kDigestMismatch, "chunk verification failed");
            }
            combined.update(ByteView(rb.data(), rb.size()));
        }
        const auto final_digest = combined.final();
        if (!crypto::equal(final_digest, crypto::sha256(data))) {
            throw_error(ErrorCode::kDigestMismatch, "checkpoint content digest mismatch");
        }

        // Deterministic canonical chunk order and gap/overlap validation.
        std::sort(manifest_entries.begin(), manifest_entries.end(),
                  [](const ManifestEntry& a, const ManifestEntry& b) {
                      return a.logical_offset < b.logical_offset;
                  });
        std::uint64_t expected = 0;
        for (const auto& e : manifest_entries) {
            if (e.logical_offset != expected) {
                throw_error(ErrorCode::kMalformed, "manifest has a gap or overlap");
            }
            expected += chunk_sizes.at(e.chunk_id);
        }
        if (expected != data.size()) {
            throw_error(ErrorCode::kMalformed, "manifest total size mismatch");
        }

        CheckpointManifest manifest;
        manifest.id = ManifestId(impl_->next_id_counter++);
        manifest.generation = ManifestGeneration(1);
        manifest.checkpoint_id = descriptor.id;
        manifest.checkpoint_generation = descriptor.generation;
        manifest.chunks = manifest_entries;
        manifest.logical_size = data.size();
        manifest.checkpoint_digest = final_digest;
        manifest.lineage = descriptor.lineage;
        manifest.parent = descriptor.parent_checkpoint;
        manifest.base = descriptor.base_checkpoint;
        manifest.provenance = descriptor.provenance;
        manifest.durability = descriptor.durability;
        manifest.required_replica_count = descriptor.required_replica_count;
        manifest.semantic_digest = manifest_semantic_digest(manifest);

        Impl::CheckpointRecord rec;
        rec.descriptor = descriptor;
        rec.manifest = manifest;
        rec.chunks = chunk_descriptors;
        rec.lifecycle = CheckpointLifecycle::kCommitted;
        rec.integrity = IntegrityState::kVerified;
        rec.freshness = Freshness::kCurrent;

        ReplicaDescriptor rd;
        rd.id = replica_id;
        rd.generation = ReplicaGeneration(1);
        rd.backend_id = impl_->primary_backend_id;
        rd.source_kind = ReplicaSourceKind::kLocal;
        rd.tier = StorageTierClass::kLocalFilesystem;
        rd.integrity = IntegrityState::kVerified;
        rd.role = ReplicaRole::kAuthoritative;
        rd.physical_size = write_size;
        rd.provenance = Provenance::kMeasured;
        impl_->replicas.emplace(rd.id, rd);

        ReplicaSet rs;
        rs.checkpoint_id = descriptor.id;
        rs.generation = descriptor.generation;
        rs.required_replica_count = descriptor.required_replica_count;
        rs.replicas.push_back(rd.id);
        rs.durability_state = descriptor.required_replica_count <= 1
                                  ? ReplicaDurabilityState::kHealthy
                                  : ReplicaDurabilityState::kUnderReplicated;
        rs.placement_generation = PlacementGeneration(1);
        rs.provenance = Provenance::kMeasured;
        rec.replica_set = rs;

        impl_->checkpoints.emplace(descriptor.id, rec);
        auto fit = impl_->families.find(descriptor.family_id);
        if (fit != impl_->families.end()) {
            fit->second.checkpoint_ids.push_back(descriptor.id);
        }
        impl_->next_checkpoint_generation[descriptor.family_id] = descriptor.generation.next();

        result.manifest = manifest;
        result.chunks = chunk_descriptors;
        result.unique_physical_bytes = write_size;
        result.deduplicated_bytes = data.size() >= write_size ? (data.size() - write_size) : 0;

        auto& acct = impl_->counters;
        acct.checkpoint_count = static_cast<std::uint64_t>(impl_->checkpoints.size());
        acct.committed_checkpoint_count = acct.checkpoint_count;
        acct.manifest_count = static_cast<std::uint64_t>(impl_->checkpoints.size());
        acct.logical_bytes += data.size();
        acct.chunk_count += chunk_descriptors.size();
        acct.blob_count = static_cast<std::uint64_t>(impl_->blobs.size());
        acct.unique_physical_bytes = dedup_.unique_physical_bytes();
        acct.deduplicated_bytes = acct.logical_bytes > acct.unique_physical_bytes
                                      ? (acct.logical_bytes - acct.unique_physical_bytes)
                                      : 0;
        acct.replica_count = static_cast<std::uint64_t>(impl_->replicas.size());
        acct.placement_count = static_cast<std::uint64_t>(impl_->placements.size());
        acct.blob_references = static_cast<std::uint64_t>(dedup_.size());
        return result;
    } catch (...) {
        // Rollback: existing shared blobs are untouched. New unreferenced blobs
        // become reclaimable; reservations are released; tombstones removed.
        for (const auto& key : new_blob_keys) {
            try {
                underlay.remove(key);
            } catch (...) {
            }
        }
        for (const auto& d : new_digests) {
            try {
                (void)dedup_.release_reference(d);
                (void)dedup_.release_until_orphan(d);
            } catch (...) {
            }
        }
        throw;
    }
}

// --------------------------------------------------------------------------
// Queries.
// --------------------------------------------------------------------------
CheckpointDescriptor CheckpointStore::get_checkpoint(CheckpointId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    return it->second.descriptor;
}

CheckpointManifest CheckpointStore::get_manifest(CheckpointId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    return it->second.manifest;
}

std::vector<ChunkDescriptor> CheckpointStore::get_chunks(CheckpointId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    return it->second.chunks;
}

std::vector<ReplicaDescriptor> CheckpointStore::get_replicas(CheckpointId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<ReplicaDescriptor> out;
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    for (const auto rid : it->second.replica_set.replicas) {
        auto rit = impl_->replicas.find(rid);
        if (rit != impl_->replicas.end()) {
            out.push_back(rit->second);
        }
    }
    return out;
}

CheckpointLifecycle CheckpointStore::lifecycle(CheckpointId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    return it->second.lifecycle;
}

bool CheckpointStore::exists(CheckpointId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->checkpoints.count(id) != 0;
}

std::vector<CheckpointId> CheckpointStore::list_checkpoints() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<CheckpointId> out;
    out.reserve(impl_->checkpoints.size());
    for (const auto& [id, rec] : impl_->checkpoints) {
        (void)rec;
        out.push_back(id);
    }
    return out;
}

void CheckpointStore::set_coordinator_epoch(CoordinatorEpoch epoch) noexcept {
    coordinator_epoch_ = epoch;
}

}  // namespace checkpointstore