#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/protocol/protocol.hpp>

#include "proto_ops.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace checkpointstore;

namespace {

class Coordinator {
public:
    Coordinator(std::filesystem::path data_root, std::uint64_t chunk_size)
        : data_root_(std::move(data_root)) {
        std::filesystem::create_directories(data_root_ / "store");
        // Persist authority bootstrap: a coordinator epoch and current boot.
        StoreOptions o;
        o.state_path = data_root_ / "metadata" / "checkpoint-store.state";
        o.boot_id = WorkerBootId(boot_);
        o.chunk_size = chunk_size;
        store_ = std::make_unique<CheckpointStore>(std::move(o));
        store_->register_backend(StorageBackendId(1),
                                 std::make_shared<LocalBackend>(data_root_ / "store", StorageBackendId(1)));
        if (std::filesystem::exists(o.state_path)) {
            store_->load_state();
        }
    }

    void set_authority(std::uint64_t boot) {
        boot_ = boot;
        std::cout << "EVIDENCE: current authority boot=" << boot_ << " epoch=" << epoch_ << "\n";
        store_->reset_authority();
    }
    std::uint64_t epoch() const { return epoch_; }
    std::uint64_t boot() const { return boot_; }

    // Returns true if the request carries the current authority.
    bool authority_ok(const proto_ops::AuthorityEnvelope& a) {
        if (a.boot != boot_) {
            ++stale_rejections_;
            return false;
        }
        if (a.epoch != epoch_) {
            ++stale_rejections_;
            return false;
        }
        return true;
    }

    void advance_epoch() {
        epoch_ += 1;
        // A killed incarnation's authority is revoked; fresh boot required.
        ++stale_rejections_;
    }

    Bytes dispatch(protocol::MessageKind kind, ByteView payload) {
        using namespace protocol;
        switch (kind) {
                case MessageKind::kRegisterBackend: {
                    auto [boot, root] = proto_ops::decode_register(payload);
                    registered_.push_back(boot);
                    // The first registered worker becomes authority (unless one set).
                    set_authority(boot);
                    std::cout << "EVIDENCE: registered worker boot=" << boot << " root=" << root << "\n";
                    return proto_ops::encode_u64(epoch_);
                }
                case MessageKind::kCreateFamily: {
                    auto owner = BinReader(payload).u64();
                    auto fam = store_->create_family(OwnerId(owner));
                    std::cout << "EVIDENCE: created family=" << fam.value() << "\n";
                    return proto_ops::encode_family_reply(fam.value());
                }
                case MessageKind::kPublish: {
                    auto req = proto_ops::decode_publish(payload);
                    if (!authority_ok(req.auth)) {
                        std::cout << "EVIDENCE: STALE publish rejected boot=" << req.auth.boot
                                  << " epoch=" << req.auth.epoch << " gen=" << req.auth.generation << "\n";
                        throw_error(ErrorCode::kStaleAuthority, "stale publish authority");
                    }
                    Bytes data(static_cast<std::size_t>(req.size));
                    for (auto& b : data) b = Byte(req.fill);
                    auto fam = CheckpointFamilyId(req.family);
                    auto d = store_->make_full_descriptor(fam, OwnerId(1), data.size());
                    d.producer_boot = WorkerBootId(req.auth.boot);
                    auto pub = store_->publish(d, ByteView(data.data(), data.size()));
                    std::cout << "EVIDENCE: committed checkpoint=" << pub.descriptor.id.value()
                              << " gen=" << d.generation.value() << " chunks=" << pub.manifest.chunks.size()
                              << " hits=" << pub.dedup_hits << " misses=" << pub.dedup_misses << "\n";
                    return proto_ops::encode_publish_reply(pub.descriptor.id.value(),
                                                           pub.manifest.chunks.size(), pub.dedup_hits,
                                                           pub.dedup_misses);
                }
                case MessageKind::kVerify: {
                    auto id = CheckpointId(proto_ops::decode_verify_id(payload));
                    auto iv = store_->verify_checkpoint(id);
                    std::cout << "EVIDENCE: verify checkpoint=" << id.value()
                              << " -> " << to_string(iv) << "\n";
                    return proto_ops::encode_u64(static_cast<std::uint64_t>(iv));
                }
                case MessageKind::kRestore: {
                    auto [auth, rid] = proto_ops::decode_restore(payload);
                    if (!authority_ok(auth)) {
                        std::cout << "EVIDENCE: STALE restore rejected boot=" << auth.boot << "\n";
                        throw_error(ErrorCode::kStaleAuthority, "stale restore authority");
                    }
                    auto rc = store_->restore(RestoreId(1), CheckpointId(rid));
                    std::cout << "EVIDENCE: restored checkpoint=" << rid
                              << " bytes=" << rc.bytes.size() << " integrity=" << to_string(rc.integrity) << "\n";
                    return proto_ops::encode_restore_reply(static_cast<std::uint64_t>(rc.integrity),
                                                           rc.bytes.size());
                }
                case MessageKind::kGcRun: {
                    auto plan = store_->gc_plan();
                    auto res = store_->gc_run();
                    std::cout << "EVIDENCE: gc reclaimed=" << res.reclaimed_blobs.size()
                              << " bytes=" << res.reclaimed_bytes << "\n";
                    return proto_ops::encode_u64(res.reclaimed_blobs.size());
                }
                case MessageKind::kSave: {
                    store_->save_state();
                    std::cout << "EVIDENCE: state saved\n";
                    return proto_ops::encode_u64(1);
                }
                case MessageKind::kAdvanceEpoch: {
                    advance_epoch();
                    std::cout << "EVIDENCE: epoch advanced to " << epoch_ << "; prior authority stale\n";
                    return proto_ops::encode_u64(epoch_);
                }
                default: {
                    throw_error(ErrorCode::kNotSupported, "unsupported request");
                }
            }
    }

    std::uint64_t stale_rejections() const { return stale_rejections_; }

private:
    std::filesystem::path data_root_;
    std::unique_ptr<CheckpointStore> store_;
    std::uint64_t boot_ = 1;
    std::uint64_t epoch_ = 1;
    std::uint64_t stale_rejections_ = 0;
    std::vector<std::uint64_t> registered_;
};

bool read_exact(SOCKET s, std::uint8_t* buf, int len) {
    int got = 0;
    while (got < len) {
        int r = recv(s, reinterpret_cast<char*>(buf + got), len - got, 0);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}
bool send_all(SOCKET s, const std::uint8_t* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int r = send(s, reinterpret_cast<const char*>(buf + sent), len - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = 33123;
    std::filesystem::path root = std::filesystem::temp_directory_path() / "cps_coordinator";
    std::uint64_t chunk = 64 * 1024;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = (std::uint16_t)std::stoi(argv[++i]);
        else if (a == "--root" && i + 1 < argc) root = argv[++i];
        else if (a == "--chunk-size" && i + 1 < argc) chunk = std::stoull(argv[++i]);
    }
    std::filesystem::create_directories(root);

    WSADATA ws;
    WSAStartup(MAKEWORD(2, 2), &ws);
    SOCKET listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET) { std::cerr << "socket failed\n"; return 1; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed " << WSAGetLastError() << "\n"; return 1;
    }
    listen(listen_fd, 8);
    std::cout << "EVIDENCE: coordinator listening on 127.0.0.1:" << port << " root=" << root.string() << "\n";

    Coordinator coord(root, chunk);
    bool running = true;
    std::cout << "READY\n";
    while (running) {
        SOCKET conn = accept(listen_fd, nullptr, nullptr);
        if (conn == INVALID_SOCKET) break;
        // Serve this connection until it closes.
        std::vector<std::uint8_t> buf;
        bool open = true;
        while (open) {
            std::uint8_t hdr[11];
            if (!read_exact(conn, hdr, 11)) { open = false; break; }
            buf.assign(hdr, hdr + 11);
            // Required total: magic(4) version(2) kind(1) length(4) + payload + crc(4).
            std::uint32_t len = (std::uint32_t)((std::uint8_t)buf[7] | ((std::uint8_t)buf[8] << 8) |
                                               ((std::uint8_t)buf[9] << 16) | ((std::uint8_t)buf[10] << 24));
            if (len > protocol::kMaxPayloadBytes) { open = false; break; }
            std::vector<std::uint8_t> rest(len + 4);
            if (!read_exact(conn, rest.data(), (int)rest.size())) { open = false; break; }
            Bytes full(reinterpret_cast<const Byte*>(buf.data()), reinterpret_cast<const Byte*>(buf.data()) + buf.size());
            full.insert(full.end(), reinterpret_cast<const Byte*>(rest.data()), reinterpret_cast<const Byte*>(rest.data()) + rest.size());
            auto frame = protocol::decode_frame(ByteView(full.data(), full.size()));
            Bytes out;
            try {
                auto reply = coord.dispatch(frame.kind, ByteView(frame.payload.data(), frame.payload.size()));
                out = protocol::encode_frame(protocol::MessageKind::kAck, ByteView(reply.data(), reply.size()));
            } catch (const CheckpointStoreError& e) {
                std::cout << "EVIDENCE: request error code=" << to_string(e.code()) << " msg=" << e.what() << "\\n";
                auto nack = proto_ops::encode_nack(e.code(), e.what());
                out = protocol::encode_frame(protocol::MessageKind::kNack, ByteView(nack.data(), nack.size()));
            }
            if (!send_all(conn, reinterpret_cast<const std::uint8_t*>(out.data()), (int)out.size())) {
                open = false;
            }
        }
        closesocket(conn);
        std::cout << "EVIDENCE: worker connection closed\n";
    }
    closesocket(listen_fd);
    WSACleanup();
    return 0;
}