#include "persistence.hpp"

#include "binformat.hpp"
#include "impl.hpp"

#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace checkpointstore {
namespace detail {

void atomic_write_file(const std::filesystem::path& path, ByteView data) {
    const auto dir = path.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto tmp = path.string() + ".tmp." + std::to_string(
        static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        std::ofstream out(std::filesystem::path(tmp), std::ios::binary | std::ios::trunc);
        if (!out) {
            throw_error(ErrorCode::kIo, "cannot open state temp for write");
        }
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out.good()) {
            std::filesystem::remove(tmp, ec);
            throw_error(ErrorCode::kIo, "state temp write failed");
        }
    }
#ifdef _WIN32
    {
        HANDLE h = ::CreateFileW(std::filesystem::path(tmp).c_str(), GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            ::FlushFileBuffers(h);
            ::CloseHandle(h);
        }
    }
#endif
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(path, ec);
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        throw_error(ErrorCode::kIo, "state rename failed");
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw_error(ErrorCode::kNotFound, "state file not found");
    }
    std::string data;
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) {
        throw_error(ErrorCode::kIo, "cannot determine state file size");
    }
    in.seekg(0, std::ios::beg);
    data.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(data.data(), size);
    }
    return data;
}

}  // namespace detail

namespace {

constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint32_t kMaxBodyBytes = 1u << 31;  // 2 GiB body ceiling
constexpr std::uint32_t kMaxRecords = 1u << 22;    // ~4M record ceiling (bounded)

std::uint32_t frame_type_u8(std::uint8_t v) { return v; }

void write_descriptor(BinWriter& w, const CheckpointDescriptor& d) {
    w.id(d.id);
    w.id(d.family_id);
    w.generation(d.generation);
    w.u8(static_cast<std::uint8_t>(d.kind));
    w.u64(d.logical_size);
    w.time_point(d.created_at);
    w.id(d.owner_id);
    w.id(d.producer_boot);
    w.generation(d.producer_generation);
    w.boolean(d.parent_checkpoint.has_value());
    if (d.parent_checkpoint) {
        w.id(*d.parent_checkpoint);
    }
    w.boolean(d.base_checkpoint.has_value());
    if (d.base_checkpoint) {
        w.id(*d.base_checkpoint);
    }
    w.u32(static_cast<std::uint32_t>(d.lineage.size()));
    for (const auto& l : d.lineage) {
        w.id(l);
    }
    w.u32(static_cast<std::uint32_t>(d.references.size()));
    for (const auto& r : d.references) {
        w.id(r);
    }
    w.string(d.compatibility);
    w.u8(static_cast<std::uint8_t>(d.retention));
    w.u8(static_cast<std::uint8_t>(d.durability));
    w.u8(static_cast<std::uint8_t>(d.restore_priority));
    w.u8(static_cast<std::uint8_t>(d.provenance));
    w.generation(d.policy_generation);
    w.u64(d.required_replica_count);
}

void read_descriptor(BinReader& r, CheckpointDescriptor& d) {
    d.id = r.id<detail::CheckpointIdTag>();
    d.family_id = r.id<detail::CheckpointFamilyIdTag>();
    d.generation = r.generation<detail::CheckpointGenerationTag>();
    d.kind = static_cast<CheckpointKind>(r.u8());
    d.logical_size = r.u64();
    d.created_at = r.time_point();
    d.owner_id = r.id<detail::OwnerIdTag>();
    d.producer_boot = r.id<detail::WorkerBootIdTag>();
    d.producer_generation = r.generation<detail::CheckpointGenerationTag>();
    if (r.boolean()) {
        d.parent_checkpoint = r.id<detail::CheckpointIdTag>();
    }
    if (r.boolean()) {
        d.base_checkpoint = r.id<detail::CheckpointIdTag>();
    }
    const auto lc = decode_count(r.u32(), kMaxRecords);
    d.lineage.clear();
    d.lineage.reserve(lc);
    for (std::size_t i = 0; i < lc; ++i) {
        d.lineage.push_back(r.id<detail::CheckpointIdTag>());
    }
    const auto dc = decode_count(r.u32(), kMaxRecords);
    d.references.clear();
    d.references.reserve(dc);
    for (std::size_t i = 0; i < dc; ++i) {
        d.references.push_back(r.id<detail::CheckpointIdTag>());
    }
    d.compatibility = r.string();
    d.retention = static_cast<RetentionClass>(r.u8());
    d.durability = static_cast<DurabilityClass>(r.u8());
    d.restore_priority = static_cast<RestorePriority>(r.u8());
    d.provenance = static_cast<Provenance>(r.u8());
    d.policy_generation = r.generation<detail::PolicyGenerationTag>();
    d.required_replica_count = r.u64();
}

void write_manifest(BinWriter& w, const CheckpointManifest& m) {
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
    w.u32(static_cast<std::uint32_t>(m.lineage.size()));
    for (const auto& l : m.lineage) {
        w.id(l);
    }
    w.boolean(m.parent.has_value());
    if (m.parent) {
        w.id(*m.parent);
    }
    w.boolean(m.base.has_value());
    if (m.base) {
        w.id(*m.base);
    }
    w.u8(static_cast<std::uint8_t>(m.provenance));
    w.u8(static_cast<std::uint8_t>(m.durability));
    w.u64(m.required_replica_count);
    w.sha256(m.semantic_digest);
}

void read_manifest(BinReader& r, CheckpointManifest& m) {
    m.id = r.id<detail::ManifestIdTag>();
    m.generation = r.generation<detail::ManifestGenerationTag>();
    m.checkpoint_id = r.id<detail::CheckpointIdTag>();
    m.checkpoint_generation = r.generation<detail::CheckpointGenerationTag>();
    const auto cc = decode_count(r.u32(), kMaxRecords);
    m.chunks.clear();
    m.chunks.reserve(cc);
    for (std::size_t i = 0; i < cc; ++i) {
        ManifestEntry e;
        e.chunk_id = r.id<detail::ChunkIdTag>();
        e.logical_offset = r.u64();
        m.chunks.push_back(e);
    }
    m.logical_size = r.u64();
    m.checkpoint_digest = r.sha256();
    const auto lc = decode_count(r.u32(), kMaxRecords);
    m.lineage.clear();
    m.lineage.reserve(lc);
    for (std::size_t i = 0; i < lc; ++i) {
        m.lineage.push_back(r.id<detail::CheckpointIdTag>());
    }
    if (r.boolean()) {
        m.parent = r.id<detail::CheckpointIdTag>();
    }
    if (r.boolean()) {
        m.base = r.id<detail::CheckpointIdTag>();
    }
    m.provenance = static_cast<Provenance>(r.u8());
    m.durability = static_cast<DurabilityClass>(r.u8());
    m.required_replica_count = r.u64();
    m.semantic_digest = r.sha256();
}

void write_chunk(BinWriter& w, const ChunkDescriptor& c) {
    w.id(c.id);
    w.generation(c.generation);
    w.u64(c.logical_offset);
    w.u64(c.logical_size);
    w.u64(c.physical_size);
    w.sha256(c.digest);
    w.id(c.blob_id);
    w.u32(static_cast<std::uint32_t>(c.placements.size()));
    for (const auto& p : c.placements) {
        w.id(p);
    }
    w.u64(c.refcount);
    w.u8(static_cast<std::uint8_t>(c.provenance));
    w.u8(static_cast<std::uint8_t>(c.integrity));
    w.boolean(c.compressed);
    w.string(c.payload_encoding);
}

void read_chunk(BinReader& r, ChunkDescriptor& c) {
    c.id = r.id<detail::ChunkIdTag>();
    c.generation = r.generation<detail::ChunkGenerationTag>();
    c.logical_offset = r.u64();
    c.logical_size = r.u64();
    c.physical_size = r.u64();
    c.digest = r.sha256();
    c.blob_id = r.id<detail::BlobIdTag>();
    const auto pc = decode_count(r.u32(), kMaxRecords);
    c.placements.clear();
    c.placements.reserve(pc);
    for (std::size_t i = 0; i < pc; ++i) {
        c.placements.push_back(r.id<detail::PlacementIdTag>());
    }
    c.refcount = r.u64();
    c.provenance = static_cast<Provenance>(r.u8());
    c.integrity = static_cast<IntegrityState>(r.u8());
    c.compressed = r.boolean();
    c.payload_encoding = r.string();
}

void write_replica_set(BinWriter& w, const ReplicaSet& s) {
    w.id(s.checkpoint_id);
    w.generation(s.generation);
    w.u64(s.required_replica_count);
    w.u32(static_cast<std::uint32_t>(s.replicas.size()));
    for (const auto& rp : s.replicas) {
        w.id(rp);
    }
    w.u8(static_cast<std::uint8_t>(s.durability_state));
    w.generation(s.placement_generation);
    w.u8(static_cast<std::uint8_t>(s.provenance));
}

void read_replica_set(BinReader& r, ReplicaSet& s) {
    s.checkpoint_id = r.id<detail::CheckpointIdTag>();
    s.generation = r.generation<detail::CheckpointGenerationTag>();
    s.required_replica_count = r.u64();
    const auto rc = decode_count(r.u32(), kMaxRecords);
    s.replicas.clear();
    s.replicas.reserve(rc);
    for (std::size_t i = 0; i < rc; ++i) {
        s.replicas.push_back(r.id<detail::ReplicaIdTag>());
    }
    s.durability_state = static_cast<ReplicaDurabilityState>(r.u8());
    s.placement_generation = r.generation<detail::PlacementGenerationTag>();
    s.provenance = static_cast<Provenance>(r.u8());
}

void write_blob(BinWriter& w, const BlobDescriptor& b) {
    w.id(b.id);
    w.generation(b.generation);
    w.u64(b.physical_size);
    w.sha256(b.digest);
    w.u64(b.refcount);
    w.u64(b.byte_references);
    w.u8(static_cast<std::uint8_t>(b.tier));
    w.u8(static_cast<std::uint8_t>(b.provenance));
    w.u8(static_cast<std::uint8_t>(b.integrity));
    w.u8(static_cast<std::uint8_t>(b.freshness));
    w.u32(static_cast<std::uint32_t>(b.replicas.size()));
    for (const auto& rp : b.replicas) {
        w.id(rp);
    }
}

void read_blob(BinReader& r, BlobDescriptor& b) {
    b.id = r.id<detail::BlobIdTag>();
    b.generation = r.generation<detail::BlobGenerationTag>();
    b.physical_size = r.u64();
    b.digest = r.sha256();
    b.refcount = r.u64();
    b.byte_references = r.u64();
    b.tier = static_cast<StorageTierClass>(r.u8());
    b.provenance = static_cast<Provenance>(r.u8());
    b.integrity = static_cast<IntegrityState>(r.u8());
    b.freshness = static_cast<Freshness>(r.u8());
    const auto rc = decode_count(r.u32(), kMaxRecords);
    b.replicas.clear();
    b.replicas.reserve(rc);
    for (std::size_t i = 0; i < rc; ++i) {
        b.replicas.push_back(r.id<detail::ReplicaIdTag>());
    }
}

void write_replica(BinWriter& w, const ReplicaDescriptor& rp) {
    w.id(rp.id);
    w.generation(rp.generation);
    w.id(rp.backend_id);
    w.u8(static_cast<std::uint8_t>(rp.source_kind));
    w.u8(static_cast<std::uint8_t>(rp.tier));
    w.u8(static_cast<std::uint8_t>(rp.integrity));
    w.u8(static_cast<std::uint8_t>(rp.role));
    w.u64(rp.physical_size);
    w.u8(static_cast<std::uint8_t>(rp.provenance));
}

void read_replica(BinReader& r, ReplicaDescriptor& rp) {
    rp.id = r.id<detail::ReplicaIdTag>();
    rp.generation = r.generation<detail::ReplicaGenerationTag>();
    rp.backend_id = r.id<detail::StorageBackendIdTag>();
    rp.source_kind = static_cast<ReplicaSourceKind>(r.u8());
    rp.tier = static_cast<StorageTierClass>(r.u8());
    rp.integrity = static_cast<IntegrityState>(r.u8());
    rp.role = static_cast<ReplicaRole>(r.u8());
    rp.physical_size = r.u64();
    rp.provenance = static_cast<Provenance>(r.u8());
}

void write_placement(BinWriter& w, const PlacementDescriptor& p) {
    w.id(p.id);
    w.generation(p.generation);
    w.id(p.replica_id);
    w.id(p.tier_id);
    w.id(p.backend_id);
    w.id(p.node_id);
    w.id(p.volume_id);
    w.u64(p.bytes);
    w.u8(static_cast<std::uint8_t>(p.provenance));
    w.u8(static_cast<std::uint8_t>(p.freshness));
}

void read_placement(BinReader& r, PlacementDescriptor& p) {
    p.id = r.id<detail::PlacementIdTag>();
    p.generation = r.generation<detail::PlacementGenerationTag>();
    p.replica_id = r.id<detail::ReplicaIdTag>();
    p.tier_id = r.id<detail::StorageTierIdTag>();
    p.backend_id = r.id<detail::StorageBackendIdTag>();
    p.node_id = r.id<detail::StorageNodeIdTag>();
    p.volume_id = r.id<detail::VolumeIdTag>();
    p.bytes = r.u64();
    p.provenance = static_cast<Provenance>(r.u8());
    p.freshness = static_cast<Freshness>(r.u8());
}

void write_retention(BinWriter& w, const RetentionPolicy& rp) {
    w.id(rp.id);
    w.id(rp.family_id);
    w.u8(static_cast<std::uint8_t>(rp.retention_class));
    w.u64(rp.latest_n);
    w.u64(static_cast<std::uint64_t>(rp.ttl.count()));
    w.boolean(rp.protect_ancestry);
    w.boolean(rp.recomputable);
    w.boolean(rp.policy_protected);
    w.generation(rp.generation);
    w.u8(static_cast<std::uint8_t>(rp.provenance));
}

void read_retention(BinReader& r, RetentionPolicy& rp) {
    rp.id = r.id<detail::RetentionPolicyIdTag>();
    rp.family_id = r.id<detail::CheckpointFamilyIdTag>();
    rp.retention_class = static_cast<RetentionClass>(r.u8());
    rp.latest_n = r.u64();
    rp.ttl = std::chrono::seconds(static_cast<std::int64_t>(r.u64()));
    rp.protect_ancestry = r.boolean();
    rp.recomputable = r.boolean();
    rp.policy_protected = r.boolean();
    rp.generation = r.generation<detail::PolicyGenerationTag>();
    rp.provenance = static_cast<Provenance>(r.u8());
}

}  // namespace
// --------------------------------------------------------------------------
// Save / load the full authoritative state.
// --------------------------------------------------------------------------
void CheckpointStore::save_state() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    BinWriter body;
    body.u64(coordinator_epoch_.value());
    body.id(options_.boot_id);
    body.u64(impl_->next_id_counter);
    body.u64(impl_->counters.integrity_failures);
    body.u64(impl_->counters.stale_rejections);
    body.u64(impl_->counters.duplicate_rejections);
    body.u64(impl_->counters.worker_restarts);
    body.u64(impl_->counters.reclaimed_blobs);
    body.u64(impl_->counters.reclaimed_bytes);
    body.u64(impl_->counters.restored_bytes);

    // Families (sorted by id for determinism).
    std::vector<Impl::FamilyRecord*> fams;
    for (auto& [k, v] : impl_->families) {
        (void)k;
        fams.push_back(&v);
    }
    std::sort(fams.begin(), fams.end(), [](auto* a, auto* b) {
        return a->id.value() < b->id.value();
    });
    body.u32(static_cast<std::uint32_t>(fams.size()));
    for (const auto* f : fams) {
        body.id(f->id);
        body.generation(f->family_generation);
        body.u32(static_cast<std::uint32_t>(f->checkpoint_ids.size()));
        for (const auto& cid : f->checkpoint_ids) {
            body.id(cid);
        }
    }

    // Checkpoint records (sorted by id).
    std::vector<Impl::CheckpointRecord*> cps;
    for (auto& [k, v] : impl_->checkpoints) {
        (void)k;
        cps.push_back(&v);
    }
    std::sort(cps.begin(), cps.end(), [](auto* a, auto* b) {
        return a->descriptor.id.value() < b->descriptor.id.value();
    });
    body.u32(static_cast<std::uint32_t>(cps.size()));
    for (const auto* c : cps) {
        write_descriptor(body, c->descriptor);
        write_manifest(body, c->manifest);
        body.u32(static_cast<std::uint32_t>(c->chunks.size()));
        for (const auto& ch : c->chunks) {
            write_chunk(body, ch);
        }
        write_replica_set(body, c->replica_set);
        body.u8(static_cast<std::uint8_t>(c->lifecycle));
        body.u8(static_cast<std::uint8_t>(c->integrity));
        body.u8(static_cast<std::uint8_t>(c->freshness));
    }

    // Blobs (sorted by id).
    std::vector<BlobDescriptor*> blobs;
    for (auto& [k, v] : impl_->blobs) {
        (void)k;
        blobs.push_back(&v);
    }
    std::sort(blobs.begin(), blobs.end(), [](auto* a, auto* b) {
        return a->id.value() < b->id.value();
    });
    body.u32(static_cast<std::uint32_t>(blobs.size()));
    for (const auto* b : blobs) {
        write_blob(body, *b);
    }

    // Replicas (sorted by id).
    std::vector<ReplicaDescriptor*> reps;
    for (auto& [k, v] : impl_->replicas) {
        (void)k;
        reps.push_back(&v);
    }
    std::sort(reps.begin(), reps.end(), [](auto* a, auto* b) {
        return a->id.value() < b->id.value();
    });
    body.u32(static_cast<std::uint32_t>(reps.size()));
    for (const auto* r : reps) {
        write_replica(body, *r);
    }

    // Placements (sorted by id).
    std::vector<PlacementDescriptor*> pls;
    for (auto& [k, v] : impl_->placements) {
        (void)k;
        pls.push_back(&v);
    }
    std::sort(pls.begin(), pls.end(), [](auto* a, auto* b) {
        return a->id.value() < b->id.value();
    });
    body.u32(static_cast<std::uint32_t>(pls.size()));
    for (const auto* p : pls) {
        write_placement(body, *p);
    }

    // Retention policies (sorted by id).
    std::vector<RetentionPolicy*> rets;
    for (auto& [k, v] : impl_->retention) {
        (void)k;
        rets.push_back(&v);
    }
    std::sort(rets.begin(), rets.end(), [](auto* a, auto* b) {
        return a->id.value() < b->id.value();
    });
    body.u32(static_cast<std::uint32_t>(rets.size()));
    for (const auto* r : rets) {
        write_retention(body, *r);
    }

    // GC state.
    body.id(impl_->gc.epoch);
    body.generation(impl_->gc.generation);
    body.u8(static_cast<std::uint8_t>(impl_->gc.phase));
    body.u64(impl_->gc.marked_bytes);
    body.u64(impl_->gc.reclaimed_bytes);

    // Dedup entries (sorted by digest hex for determinism).
    auto dd_entries = dedup_.snapshot();
    std::sort(dd_entries.begin(), dd_entries.end(), [](const auto& a, const auto& b) {
        return crypto::hex(a.first) < crypto::hex(b.first);
    });
    body.u32(static_cast<std::uint32_t>(dd_entries.size()));
    for (const auto& [digest, e] : dd_entries) {
        body.sha256(digest);
        body.u64(e.physical_size);
        body.u64(e.refcount);
        body.u8(static_cast<std::uint8_t>(e.integrity));
        body.id(e.blob_id);
        body.generation(e.generation);
    }

    // Frame.
    const auto& body_bytes = body.data();
    BinWriter frame;
    std::array<std::uint8_t, 4> magic = {'C', 'P', 'S', 'T'};
    frame.bytes_fixed(ByteView(reinterpret_cast<const Byte*>(magic.data()), magic.size()));
    frame.u32(kFormatVersion);
    frame.u32(static_cast<std::uint32_t>(body_bytes.size()));
    frame.bytes_fixed(ByteView(body_bytes.data(), body_bytes.size()));
    frame.u32(crypto::crc32(ByteView(body_bytes.data(), body_bytes.size())));
    frame.sha256(crypto::sha256(ByteView(body_bytes.data(), body_bytes.size())));

    detail::atomic_write_file(options_.state_path, ByteView(frame.data().data(), frame.data().size()));
}

void CheckpointStore::load_state() {
    const std::string raw = detail::read_file(options_.state_path);
    ByteView view(reinterpret_cast<const Byte*>(raw.data()), raw.size());
    if (view.size() < 4 + 4 + 4 + 4 + 32) {
        throw_error(ErrorCode::kTruncated, "state frame too small");
    }
    BinReader frame(view);
    const auto magic = frame.bytes_fixed(4);
    const std::array<std::uint8_t, 4> expected = {'C', 'P', 'S', 'T'};
    if (!bytes_equal(magic, ByteView(reinterpret_cast<const Byte*>(expected.data()), 4))) {
        throw_error(ErrorCode::kBadMagic, "state magic mismatch");
    }
    const std::uint32_t version = frame.u32();
    if (version != kFormatVersion) {
        throw_error(ErrorCode::kUnsupportedVersion, "state version unsupported");
    }
    const std::uint32_t body_len = frame.u32();
    if (body_len > kMaxBodyBytes) {
        throw_error(ErrorCode::kMalformed, "state body too large");
    }
    const auto body = frame.bytes_fixed(body_len);
    const std::uint32_t stored_crc = frame.u32();
    const auto stored_digest = frame.sha256();
    if (frame.remaining() != 0) {
        throw_error(ErrorCode::kMalformed, "state trailing garbage");
    }
    if (crypto::crc32(body) != stored_crc) {
        throw_error(ErrorCode::kChecksumMismatch, "state body checksum mismatch");
    }
    if (!crypto::equal(crypto::sha256(body), stored_digest)) {
        throw_error(ErrorCode::kDigestMismatch, "state body digest mismatch");
    }

    // Rebuild authoritative state.
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->next_id_counter = 1000;
    impl_->counters = AccountingSnapshot{};
    impl_->families.clear();
    impl_->checkpoints.clear();
    impl_->blobs.clear();
    impl_->replicas.clear();
    impl_->placements.clear();
    impl_->retention.clear();
    impl_->gc = GcState{};

    BinReader r(body);
    coordinator_epoch_ = CoordinatorEpoch(r.u64());
    options_.boot_id = r.id<detail::WorkerBootIdTag>();
    impl_->next_id_counter = r.u64();
    impl_->counters.integrity_failures = r.u64();
    impl_->counters.stale_rejections = r.u64();
    impl_->counters.duplicate_rejections = r.u64();
    impl_->counters.worker_restarts = r.u64();
    impl_->counters.reclaimed_blobs = r.u64();
    impl_->counters.reclaimed_bytes = r.u64();
    impl_->counters.restored_bytes = r.u64();

    const auto fam_count = decode_count(r.u32(), kMaxRecords);
    for (std::size_t i = 0; i < fam_count; ++i) {
        Impl::FamilyRecord f;
        f.id = r.id<detail::CheckpointFamilyIdTag>();
        f.family_generation = r.generation<detail::CheckpointFamilyGenerationTag>();
        const auto cc = decode_count(r.u32(), kMaxRecords);
        f.checkpoint_ids.reserve(cc);
        for (std::size_t j = 0; j < cc; ++j) {
            f.checkpoint_ids.push_back(r.id<detail::CheckpointIdTag>());
        }
        if (impl_->families.count(f.id)) {
            throw_error(ErrorCode::kMalformed, "duplicate family id in state");
        }
        impl_->families.emplace(f.id, f);
        impl_->next_checkpoint_generation[f.id] = CheckpointGeneration(1);
    }

    const auto cp_count = decode_count(r.u32(), kMaxRecords);
    for (std::size_t i = 0; i < cp_count; ++i) {
        Impl::CheckpointRecord rec;
        read_descriptor(r, rec.descriptor);
        read_manifest(r, rec.manifest);
        const auto ccount = decode_count(r.u32(), kMaxRecords);
        rec.chunks.reserve(ccount);
        for (std::size_t j = 0; j < ccount; ++j) {
            ChunkDescriptor ch;
            read_chunk(r, ch);
            rec.chunks.push_back(ch);
        }
        read_replica_set(r, rec.replica_set);
        rec.lifecycle = static_cast<CheckpointLifecycle>(r.u8());
        rec.integrity = static_cast<IntegrityState>(r.u8());
        rec.freshness = static_cast<Freshness>(r.u8());
        if (impl_->checkpoints.count(rec.descriptor.id)) {
            throw_error(ErrorCode::kMalformed, "duplicate checkpoint id in state");
        }
        impl_->checkpoints.emplace(rec.descriptor.id, rec);
        auto& ng = impl_->next_checkpoint_generation[rec.descriptor.family_id];
        if (ng.precedes(rec.descriptor.generation)) {
            ng = rec.descriptor.generation;
        }
    }

    // Advance each family's next generation past the highest loaded generation.
    for (auto& [fam, ng] : impl_->next_checkpoint_generation) {
        (void)fam;
        ng = ng.next();
    }

    const auto blob_count = decode_count(r.u32(), kMaxRecords);
    for (std::size_t i = 0; i < blob_count; ++i) {
        BlobDescriptor b;
        read_blob(r, b);
        if (impl_->blobs.count(b.id)) {
            throw_error(ErrorCode::kMalformed, "duplicate blob id in state");
        }
        impl_->blobs.emplace(b.id, b);
    }

    const auto rep_count = decode_count(r.u32(), kMaxRecords);
    for (std::size_t i = 0; i < rep_count; ++i) {
        ReplicaDescriptor rp;
        read_replica(r, rp);
        if (impl_->replicas.count(rp.id)) {
            throw_error(ErrorCode::kMalformed, "duplicate replica id in state");
        }
        impl_->replicas.emplace(rp.id, rp);
    }

    const auto pl_count = decode_count(r.u32(), kMaxRecords);
    for (std::size_t i = 0; i < pl_count; ++i) {
        PlacementDescriptor p;
        read_placement(r, p);
        if (impl_->placements.count(p.id)) {
            throw_error(ErrorCode::kMalformed, "duplicate placement id in state");
        }
        impl_->placements.emplace(p.id, p);
    }

    const auto ret_count = decode_count(r.u32(), kMaxRecords);
    for (std::size_t i = 0; i < ret_count; ++i) {
        RetentionPolicy rp;
        read_retention(r, rp);
        if (impl_->retention.count(rp.id)) {
            throw_error(ErrorCode::kMalformed, "duplicate retention id in state");
        }
        impl_->retention.emplace(rp.id, rp);
    }

    impl_->gc.epoch = r.id<detail::GcEpochIdTag>();
    impl_->gc.generation = r.generation<detail::GcGenerationTag>();
    impl_->gc.phase = static_cast<GcPhase>(r.u8());
    impl_->gc.marked_bytes = r.u64();
    impl_->gc.reclaimed_bytes = r.u64();

    const auto dedup_count = decode_count(r.u32(), kMaxRecords);
    for (std::size_t i = 0; i < dedup_count; ++i) {
        const auto digest = r.sha256();
        const auto size = r.u64();
        const auto refc = r.u64();
        const auto integrity = static_cast<IntegrityState>(r.u8());
        const auto blob = r.id<detail::BlobIdTag>();
        const auto gen = r.generation<detail::BlobGenerationTag>();
        if (dedup_.contains(digest)) {
            throw_error(ErrorCode::kMalformed, "duplicate dedup entry in state");
        }
        DedupEntry e;
        e.blob_id = blob;
        e.digest = digest;
        e.physical_size = size;
        e.refcount = refc;
        e.integrity = integrity;
        e.generation = gen;
        dedup_.insert_blob_entry_direct(e);
    }

    if (r.remaining() != 0) {
        throw_error(ErrorCode::kMalformed, "state body trailing garbage");
    }
    // Recompute derived accounting counters from the recovered records.
    auto& acct = impl_->counters;
    acct.family_count = static_cast<std::uint64_t>(impl_->families.size());
    acct.checkpoint_count = static_cast<std::uint64_t>(impl_->checkpoints.size());
    acct.manifest_count = acct.checkpoint_count;
    acct.blob_count = static_cast<std::uint64_t>(impl_->blobs.size());
    acct.replica_count = static_cast<std::uint64_t>(impl_->replicas.size());
    acct.placement_count = static_cast<std::uint64_t>(impl_->placements.size());
    acct.chunk_count = 0;
    acct.logical_bytes = 0;
    acct.committed_checkpoint_count = 0;
    for (const auto& [cid, rec] : impl_->checkpoints) {
        (void)cid;
        acct.chunk_count += rec.chunks.size();
        acct.logical_bytes += rec.descriptor.logical_size;
        if (rec.lifecycle == CheckpointLifecycle::kCommitted) {
            ++acct.committed_checkpoint_count;
        }
    }
    acct.unique_physical_bytes = dedup_.unique_physical_bytes();
    acct.deduplicated_bytes =
        acct.logical_bytes >= acct.unique_physical_bytes
            ? (acct.logical_bytes - acct.unique_physical_bytes)
            : 0;
    acct.blob_references = static_cast<std::uint64_t>(dedup_.size());
    // After load, live worker/process authority must be cleared; the
    // coordinator advances the epoch on recovery.
}

}  // namespace checkpointstore