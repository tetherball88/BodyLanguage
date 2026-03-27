#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <Windows.h>
#include <wincrypt.h>

namespace BodyLanguage {

// Computes an MD5 hash by calling feedFn(hHash) to feed data into the context.
// feedFn must call CryptHashData one or more times and return true on success.
// Returns the lowercase hex digest, or "" on any failure.
std::string ComputeMD5(std::function<bool(HCRYPTHASH)> feedFn);

// Convenience overload: hashes the contents of a file on disk.
std::string ComputeMD5(const std::filesystem::path& path);

}  // namespace BodyLanguage
