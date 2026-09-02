#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/storage/synthetic_backend.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/explain.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/tier.hpp>
#include <checkpointstore/replication.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace checkpointstore;

namespace {

struct Cli {
    std::filesystem::path root;
    std::filesystem::path state_path;
    std::filesystem::path store_root;
    WorkerBootId boot{1};
    std::uint64_t chunk_size = 64 * 1024;

    std::unique_ptr<CheckpointStore> make() {
        StoreOptions o;
        o.state_path = state_path;
        o.boot_id = boot;
        o.chunk_size = chunk_size;
        auto s = std::make_unique<CheckpointStore>(std::move(o));
        s->register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(store_root, StorageBackendId(1)));
        if (std::filesystem::exists(state_path)) {
            s->load_state();
        }
        return s;
    }
};

std::uint64_t parse_u64(const std::string& s) {
    std::uint64_t v = 0;
    std::istringstream iss(s);
    iss >> v;
    return v;
}
CheckpointId parse_id(const std::string& s) { return CheckpointId(parse_u64(s)); }
CheckpointFamilyId parse_family(const std::string& s) { return CheckpointFamilyId(parse_u64(s)); }
WorkerBootId parse_boot(const std::string& s) { return WorkerBootId(parse_u64(s)); }

std::string opt(const std::vector<std::string>& args, const std::string& name, const std::string& def = "") {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) return args[i + 1];
    }
    return def;
}
bool has(const std::vector<std::string>& args, const std::string& name) {
    for (const auto& a : args) if (a == name) return true;
    return false;
}

void print_checkpoint(const CheckpointDescriptor& d, const CheckpointManifest& m,
                      const std::vector<ChunkDescriptor>& chunks,
                      const std::vector<ReplicaDescriptor>& reps) {
    std::uint64_t logical = d.logical_size;
    std::uint64_t unique = 0;
    for (const auto& c : chunks) unique += c.physical_size;
    std::cout << "id=" << to_hex_string(d.id) << "\n"
              << "family=" << to_hex_string(d.family_id) << "\n"
              << "generation=" << d.generation.value() << "\n"
              << "kind=" << to_string(d.kind) << "\n"
              << "digest=" << crypto::hex(m.checkpoint_digest) << "\n"
              << "manifest=" << to_hex_string(m.id) << "\n"
              << "chunks=" << m.chunks.size() << "\n"
              << "logical_bytes=" << logical << "\n"
              << "physical_bytes=" << unique << "\n"
              << "dedup_bytes=" << (logical >= unique ? logical - unique : 0) << "\n"
              << "retention=" << to_string(d.retention) << "\n"
              << "durability=" << to_string(d.durability) << "\n"
              << "provenance=" << to_string(d.provenance) << "\n"
              << "replicas=" << reps.size() << "\n"
              << "authority_boot=" << d.producer_boot.value() << "\n";
}

Bytes deterministic_data(char fill, std::uint64_t size) {
    Bytes b(static_cast<std::size_t>(size));
    for (auto& x : b) x = static_cast<Byte>(fill);
    return b;
}

int run(const std::vector<std::string>& args) {
    Cli cli;
    cli.root = std::filesystem::current_path() / "checkpoint-store-data";
    std::string command = args.empty() ? "" : args[0];
    std::vector<std::string> rest(args.begin() + (args.empty() ? 0 : 1), args.end());
    // Parse --root / --boot / --chunk-size.
    if (has(rest, "--root")) cli.root = std::filesystem::path(opt(rest, "--root"));
    if (has(rest, "--boot")) cli.boot = parse_boot(opt(rest, "--boot"));
    if (has(rest, "--chunk-size")) cli.chunk_size = parse_u64(opt(rest, "--chunk-size"));
    cli.state_path = cli.root / "metadata" / "checkpoint-store.state";
    cli.store_root = cli.root / "store";
    std::filesystem::create_directories(cli.store_root);

    auto s = cli.make();

    if (command == "family-create") {
        OwnerId owner(parse_u64(opt(rest, "--owner", "1")));
        auto fam = s->create_family(owner);
        std::cout << "family=" << to_hex_string(fam) << "\n";
        s->save_state();
        return 0;
    }
    if (command == "checkpoint-create") {
        auto fam = parse_family(opt(rest, "--family"));
        std::uint64_t size = parse_u64(opt(rest, "--size", "1048576"));
        char fill = has(rest, "--fill") ? opt(rest, "--fill")[0] : 'A';
        Bytes data = deterministic_data(fill, size);
        auto d = s->make_full_descriptor(fam, OwnerId(parse_u64(opt(rest, "--owner", "1"))), data.size());
        auto pub = s->publish(d, ByteView(data.data(), data.size()));
        std::cout << "published id=" << to_hex_string(pub.descriptor.id) << "\n";
        print_checkpoint(pub.descriptor, pub.manifest, pub.chunks, s->get_replicas(pub.descriptor.id));
        std::cout << "dedup_hits=" << pub.dedup_hits << " dedup_misses=" << pub.dedup_misses << "\n";
        s->save_state();
        return 0;
    }
    if (command == "checkpoint-show") {
        auto id = parse_id(opt(rest, "--id"));
        auto d = s->get_checkpoint(id);
        auto m = s->get_manifest(id);
        auto ch = s->get_chunks(id);
        print_checkpoint(d, m, ch, s->get_replicas(id));
        return 0;
    }
    if (command == "verify") {
        auto id = parse_id(opt(rest, "--id"));
        auto iv = s->verify_checkpoint(id);
        std::cout << "integrity=" << to_string(iv) << "\n";
        return iv == IntegrityState::kVerified ? 0 : 1;
    }
    if (command == "restore") {
        auto id = parse_id(opt(rest, "--id"));
        auto rc = s->restore(RestoreId(parse_u64(opt(rest, "--restore-id", "1"))), id);
        std::cout << "bytes=" << rc.bytes.size() << " integrity=" << to_string(rc.integrity)
                  << " source_replica=" << rc.evidence.source_replica.value()
                  << " duration_ms=" << rc.evidence.duration.count() << "\n";
        if (has(rest, "--out")) {
            std::ofstream out(std::filesystem::path(opt(rest, "--out")), std::ios::binary);
            out.write(reinterpret_cast<const char*>(rc.bytes.data()), (std::streamsize)rc.bytes.size());
            std::cout << "wrote=" << opt(rest, "--out") << "\n";
        }
        return 0;
    }
    if (command == "manifest") {
        auto id = parse_id(opt(rest, "--id"));
        auto m = s->get_manifest(id);
        std::cout << "manifest=" << to_hex_string(m.id) << " generation=" << m.generation.value()
                  << " chunks=" << m.chunks.size() << " logical_size=" << m.logical_size
                  << " digest=" << crypto::hex(m.semantic_digest) << "\n";
        for (const auto& e : m.chunks) {
            std::cout << "  chunk=" << to_hex_string(e.chunk_id.value()) << " offset=" << e.logical_offset << "\n";
        }
        return 0;
    }
    if (command == "chunks") {
        auto id = parse_id(opt(rest, "--id"));
        auto ch = s->get_chunks(id);
        std::cout << "chunks=" << ch.size() << "\n";
        for (const auto& c : ch) {
            std::cout << "  id=" << to_hex_string(c.id.value()) << " offset=" << c.logical_offset
                      << " size=" << c.logical_size << " digest=" << crypto::hex(c.digest)
                      << " integrity=" << to_string(c.integrity) << "\n";
        }
        return 0;
    }
    if (command == "replicas") {
        auto id = parse_id(opt(rest, "--id"));
        auto reps = s->get_replicas(id);
        std::cout << "replicas=" << reps.size() << "\n";
        for (const auto& r : reps) {
            std::cout << "  replica=" << to_hex_string(r.id.value()) << " backend=" << r.backend_id.value()
                      << " tier=" << to_string(r.tier) << " integrity=" << to_string(r.integrity)
                      << " role=" << to_string(r.role) << " provenance=" << to_string(r.provenance) << "\n";
        }
        return 0;
    }
    if (command == "retain") {
        auto fam = parse_family(opt(rest, "--family"));
        RetentionPolicy p;
        p.id = RetentionPolicyId(parse_u64(opt(rest, "--id", "1")));
        p.family_id = fam;
        p.retention_class = RetentionClass::kKeepLatestN;
        p.latest_n = parse_u64(opt(rest, "--latest", "5"));
        s->set_retention_policy(p);
        std::cout << "retain family=" << to_hex_string(fam) << " latest_n=" << p.latest_n << "\n";
        s->save_state();
        return 0;
    }
    if (command == "retire") {
        auto id = parse_id(opt(rest, "--id"));
        s->retire(id);
        std::cout << "retired=" << to_hex_string(id) << "\n";
        s->save_state();
        return 0;
    }
    if (command == "gc-plan") {
        auto plan = s->gc_plan();
        std::cout << "gc_epoch=" << plan.epoch.value() << " generation=" << plan.generation.value()
                  << " marked_bytes=" << plan.marked_bytes << "\n";
        return 0;
    }
    if (command == "gc-run") {
        auto plan = s->gc_plan();
        auto res = s->gc_run();
        std::cout << "gc_epoch=" << res.epoch.value() << " generation=" << res.generation.value()
                  << " reclaimed_blobs=" << res.reclaimed_blobs.size()
                  << " reclaimed_bytes=" << res.reclaimed_bytes
                  << " dereferenced=" << res.dereferenced_blobs << "\n";
        s->save_state();
        return 0;
    }
    if (command == "explain") {
        auto id = parse_id(opt(rest, "--id"));
        auto d = s->get_checkpoint(id);
        std::cout << explain_checkpoint(d) << "\n";
        std::cout << s->explain_integrity(id) << "\n";
        return 0;
    }
    if (command == "save") {
        s->save_state();
        std::cout << "saved=" << cli.state_path.string() << "\n";
        return 0;
    }
    if (command == "recover") {
        // Simulate coordinator restart: reload state, clear authority, re-verify.
        auto s2 = cli.make();
        s2->reset_authority();
        std::cout << "recovered state=" << cli.state_path.string() << "\n";
        for (auto id : s2->list_checkpoints()) {
            auto iv = s2->verify_checkpoint(id);
            std::cout << "  checkpoint=" << to_hex_string(id.value()) << " integrity=" << to_string(iv) << "\n";
        }
        return 0;
    }
    if (command == "simulate") {
        // Synthetic multi-tier scenario: local fast + remote durable + archival.
        auto fam = s->create_family(OwnerId(1));
        Bytes data = deterministic_data('S', 256 * 1024);
        auto d = s->make_full_descriptor(fam, OwnerId(1), data.size());
        auto pub = s->publish(d, ByteView(data.data(), data.size()));
        std::cout << "simulated publish id=" << to_hex_string(pub.descriptor.id) << "\n";
        auto cap = s->capacity_report();
        std::cout << "capacity: total=" << cap.total_bytes << " free=" << cap.free_bytes
                  << " provenance=" << to_string(cap.provenance) << " freshness=" << to_string(cap.freshness) << "\n";
        s->save_state();
        return 0;
    }
    if (command == "benchmark") {
        auto fam = s->create_family(OwnerId(1));
        std::uint64_t size = parse_u64(opt(rest, "--size", "16777216"));
        Bytes data = deterministic_data('B', size);
        auto t0 = std::chrono::steady_clock::now();
        auto d = s->make_full_descriptor(fam, OwnerId(1), data.size());
        auto pub = s->publish(d, ByteView(data.data(), data.size()));
        auto t1 = std::chrono::steady_clock::now();
        std::cout << "pub_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << "\n";
        auto t2 = std::chrono::steady_clock::now();
        auto rc = s->restore(RestoreId(7), d.id);
        auto t3 = std::chrono::steady_clock::now();
        std::cout << "restore_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count()
                  << " bytes=" << rc.bytes.size() << " size=" << size << " chunk_size=" << cli.chunk_size << "\n";
        s->save_state();
        return 0;
    }
    std::cout << "usage: checkpoint-store <command> [--root DIR] [--boot N] ...\n"
              << "commands: family-create checkpoint-create checkpoint-show verify restore manifest chunks "
                 "replicas retain retire gc-plan gc-run explain save recover simulate benchmark\n";
    return 2;
}
}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    try {
        return run(args);
    } catch (const CheckpointStoreError& e) {
        std::cerr << "error[" << to_string(e.code()) << "]: " << e.what() << "\n";
        return 1;
    }
}