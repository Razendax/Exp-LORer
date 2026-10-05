#include "StandardFileHasher.h"

#include <array>
#include <fstream>

#include <xxhash.h>

#include "Logging.h"
#include "WindowsLongPath.h"

Result<std::uint64_t> StandardFileHasher::hashFileContent(const std::filesystem::path& path)
{
    const std::filesystem::path target = WindowsLongPath::withPrefix(path);

    std::ifstream stream(target, std::ios::binary);
    if (!stream)
    {
        Logging::debug("StandardFileHasher: failed to open file for hashing");
        return Result<std::uint64_t>::failure(Error(ErrorCode::IoError, "Failed to open file for hashing"));
    }

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);

    constexpr std::size_t chunkSize = 256 * 1024;
    std::array<char, chunkSize> buffer{};

    while (stream)
    {
        stream.read(buffer.data(), static_cast<std::streamsize>(chunkSize));
        const auto bytesRead = static_cast<std::size_t>(stream.gcount());
        if (bytesRead == 0)
        {
            break;
        }
        XXH64_update(state, buffer.data(), bytesRead);
    }

    const std::uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);

    return Result<std::uint64_t>::success(hash);
}
