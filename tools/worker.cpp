#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/protocol/protocol.hpp>
#include <checkpointstore/base/byte.hpp>

#include "proto_ops.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace checkpointstore;
namespace P = protocol;

namespace {

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

class Client {
public:
    std::uint64_t boot = 1;
    std::uint64_t epoch = 1;
    SOCKET fd = INVALID_SOCKET;

    bool connect_to(const char* host, std::uint16_t port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &addr.sin_addr);
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == INVALID_SOCKET) return false;
        if (connect(fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) return false;
        return true;
    }

    Bytes request(P::MessageKind kind, ByteView payload) {
        auto frame = P::encode_frame(kind, payload);
        if (!send_all(fd, reinterpret_cast<const std::uint8_t*>(frame.data()), (int)frame.size()))
            throw_error(ErrorCode::kIo, "send failed");
        std::uint8_t hdr[11];
        if (!read_exact(fd, hdr, 11)) throw_error(ErrorCode::kTruncated, "reply header missing");
        std::uint32_t len = (std::uint32_t)((std::uint8_t)hdr[7] | ((std::uint8_t)hdr[8] << 8) |
                                            ((std::uint8_t)hdr[9] << 16) | ((std::uint8_t)hdr[10] << 24));
        std::vector<std::uint8_t> rest(len + 4);
        if (!read_exact(fd, rest.data(), (int)rest.size())) throw_error(ErrorCode::kTruncated, "reply truncated");
        Bytes full(reinterpret_cast<const Byte*>(hdr), reinterpret_cast<const Byte*>(hdr) + 11);
        full.insert(full.end(), reinterpret_cast<const Byte*>(rest.data()), reinterpret_cast<const Byte*>(rest.data()) + rest.size());
        auto dec = P::decode_frame(ByteView(full.data(), full.size()));
        if (dec.kind == P::MessageKind::kNack) {
            auto n = proto_ops::decode_nack(ByteView(dec.payload.data(), dec.payload.size()));
            throw_error(n.code, n.message);
        }
        return dec.payload;
    }
};

std::uint64_t parse_u64(const std::string& s) { return std::stoull(s); }

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 33123;
    std::uint64_t boot = 1;
    std::uint64_t epoch = 1;
    std::string command;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--host" && i + 1 < argc) host = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = (std::uint16_t)std::stoi(argv[++i]);
        else if (a == "--boot" && i + 1 < argc) boot = parse_u64(argv[++i]);
        else if (a == "--epoch" && i + 1 < argc) epoch = parse_u64(argv[++i]);
        else { if (command.empty()) command = a; else args.push_back(a); }
    }
    WSADATA ws;
    WSAStartup(MAKEWORD(2, 2), &ws);
    Client c;
    c.boot = boot;
    c.epoch = epoch;
    if (!c.connect_to(host.c_str(), port)) { std::cerr << "worker: connect failed\n"; WSACleanup(); return 1; }

    try {
        if (command == "register") {
            auto root = args.size() ? args[0] : std::string("cps-worker");
            auto enc = proto_ops::encode_register(boot, root);
            auto reply = c.request(P::MessageKind::kRegisterBackend, ByteView(enc.data(), enc.size()));
            c.epoch = BinReader(ByteView(reply.data(), reply.size())).u64();
            std::cout << "WORKER[" << boot << "] registered; coordinator epoch=" << c.epoch << "\n";
        } else if (command == "family") {
            auto enc = proto_ops::encode_u64(parse_u64(args[0]));
            auto reply = c.request(P::MessageKind::kCreateFamily, ByteView(enc.data(), enc.size()));
            auto fam = proto_ops::decode_family_reply(ByteView(reply.data(), reply.size()));
            std::cout << "WORKER[" << boot << "] family=" << fam << "\n";
        } else if (command == "publish") {
            // args: family fill size
            std::uint64_t family = parse_u64(args[0]);
            char fill = args[1][0];
            std::uint64_t size = parse_u64(args[2]);
            proto_ops::AuthorityEnvelope a; a.boot = boot; a.epoch = c.epoch; a.generation = 1;
            auto enc = proto_ops::encode_publish(a, family, fill, size);
            auto reply = c.request(P::MessageKind::kPublish, ByteView(enc.data(), enc.size()));
            auto pr = proto_ops::decode_publish_reply(ByteView(reply.data(), reply.size()));
            std::cout << "WORKER[" << boot << "] published id=" << pr.id << " chunks=" << pr.chunks
                      << " hits=" << pr.hits << " misses=" << pr.misses << "\n";
        } else if (command == "verify") {
            auto enc = proto_ops::encode_u64(parse_u64(args[0]));
            auto reply = c.request(P::MessageKind::kVerify, ByteView(enc.data(), enc.size()));
            auto iv = BinReader(ByteView(reply.data(), reply.size())).u64();
            std::cout << "WORKER[" << boot << "] verify id=" << args[0] << " integrity=" << iv << "\n";
        } else if (command == "restore") {
            std::uint64_t id = parse_u64(args[0]);
            proto_ops::AuthorityEnvelope a; a.boot = boot; a.epoch = c.epoch; a.generation = 1;
            auto enc = proto_ops::encode_restore(a, id);
            auto reply = c.request(P::MessageKind::kRestore, ByteView(enc.data(), enc.size()));
            auto [iv, bytes] = proto_ops::decode_restore_reply(ByteView(reply.data(), reply.size()));
            std::cout << "WORKER[" << boot << "] restore id=" << id << " integrity=" << iv << " bytes=" << bytes << "\n";
        } else if (command == "advance") {
            auto enc = proto_ops::encode_u64(0);
            auto reply = c.request(P::MessageKind::kAdvanceEpoch, ByteView(enc.data(), enc.size()));
            auto e = BinReader(ByteView(reply.data(), reply.size())).u64();
            std::cout << "WORKER[" << boot << "] epoch advanced to " << e << "\n";
        } else if (command == "save") {
            auto enc = proto_ops::encode_u64(0);
            auto reply = c.request(P::MessageKind::kSave, ByteView(enc.data(), enc.size()));
            std::cout << "WORKER[" << boot << "] saved\n";
        } else if (command == "gc") {
            proto_ops::AuthorityEnvelope a; a.boot = boot; a.epoch = c.epoch; a.generation = 1;
            auto enc = proto_ops::encode_restore(a, 0);
            auto reply = c.request(P::MessageKind::kGcRun, ByteView(enc.data(), enc.size()));
            auto n = BinReader(ByteView(reply.data(), reply.size())).u64();
            std::cout << "WORKER[" << boot << "] gc reclaimed=" << n << "\n";
        } else if (command == "retain") {
            auto enc = proto_ops::encode_retain(parse_u64(args[0]), parse_u64(args[1]));
            auto reply = c.request(P::MessageKind::kRetain, ByteView(enc.data(), enc.size()));
            std::cout << "WORKER[" << boot << "] retention set latest=" << BinReader(ByteView(reply.data(), reply.size())).u64() << "\n";
        } else if (command == "retire") {
            std::uint64_t id = parse_u64(args[0]);
            proto_ops::AuthorityEnvelope a; a.boot = boot; a.epoch = c.epoch; a.generation = 1;
            auto enc = proto_ops::encode_restore(a, id);
            auto reply = c.request(P::MessageKind::kRetire, ByteView(enc.data(), enc.size()));
            std::cout << "WORKER[" << boot << "] retired id=" << id << "\n";
        } else if (command == "hold") {
            // A long-lived worker session: register, then optionally publish, then
            // sleep so it can be killed as a real OS process while holding authority.
            auto root = args.size() ? args[0] : std::string("cps-worker");
            auto enc = proto_ops::encode_register(boot, root);
            auto reply = c.request(P::MessageKind::kRegisterBackend, ByteView(enc.data(), enc.size()));
            c.epoch = BinReader(ByteView(reply.data(), reply.size())).u64();
            std::cout << "WORKER[" << boot << "] holding (registered; epoch=" << c.epoch << ")" << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(3600));
        } else if (command == "sleep") {
            std::this_thread::sleep_for(std::chrono::milliseconds(parse_u64(args[0])));
            std::cout << "WORKER[" << boot << "] slept\n";
        } else {
            std::cout << "worker: unknown command " << command << "\n";
        }
    } catch (const CheckpointStoreError& e) {
        std::cout << "WORKER[" << boot << "] ERROR code=" << to_string(e.code()) << " msg=" << e.what() << "\n";
    }
    closesocket(c.fd);
    WSACleanup();
    return 0;
}