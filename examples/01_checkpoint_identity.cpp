#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    // Strong, non-interchangeable identities.
    CheckpointId cp(7), cp2(7);
    ChunkId ch(7);
    std::cout << "checkpoint id " << to_hex_string(cp) << " == replicate " << to_hex_string(cp2) << "\n";
    std::cout << "generation ordering: g1<g2 " << (CheckpointGeneration(1).precedes(CheckpointGeneration(2))) << "\n";
    // Authority is incarnation-scoped.
    WorkerBootId boot(42);
    ScopedAuthority<CheckpointGeneration> auth(boot, CheckpointGeneration(5));
    std::cout << "authority boot=" << auth.boot().value() << " gen=" << auth.generation().value() << "\n";
    // (ch is a different identity kind and is never interchangeable with cp.)
    (void)ch;
    return 0;
}
