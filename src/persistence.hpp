#ifndef CHECKPOINTSTORE_PERSISTENCE_HPP
#define CHECKPOINTSTORE_PERSISTENCE_HPP

#include <checkpointstore/base/byte.hpp>

#include <filesystem>
#include <string>

namespace checkpointstore::detail {

// Atomically writes a file: temp file in the same directory -> flush -> close
// -> rename over the target -> fsync the parent directory best-effort.
void atomic_write_file(const std::filesystem::path& path, ByteView data);

// Reads a file; throws on an unreadable or oversized file.
std::string read_file(const std::filesystem::path& path);

}  // namespace checkpointstore::detail

#endif
