#include "HashUtils.h"

#include <fstream>

namespace BodyLanguage {

std::string ComputeMD5(std::function<bool(HCRYPTHASH)> feedFn)
{
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return {};
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return {};
    }

    const bool ok = feedFn(hHash);

    if (!ok) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return {};
    }

    DWORD hashLen    = 16;
    BYTE  hashBytes[16]{};
    CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashLen, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(32);
    for (BYTE b : hashBytes) {
        hex += kDigits[(b >> 4) & 0xF];
        hex += kDigits[b & 0xF];
    }
    return hex;
}

std::string ComputeMD5(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};

    return ComputeMD5([&](HCRYPTHASH hHash) -> bool {
        char buf[65536];
        while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
            if (!CryptHashData(hHash,
                    reinterpret_cast<const BYTE*>(buf),
                    static_cast<DWORD>(f.gcount()), 0))
                return false;
        }
        return true;
    });
}

}  // namespace BodyLanguage
