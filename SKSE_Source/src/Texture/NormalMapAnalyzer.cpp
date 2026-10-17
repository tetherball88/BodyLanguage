#include "PCH.h"

#include "Texture/NormalMapAnalyzer.h"

#include <DirectXTex.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <mutex>
#include <vector>

#include <spdlog/spdlog.h>

namespace BodyLanguage {

    namespace {

        std::string ToLowerAscii(std::string_view input) {
            std::string out(input);
            std::ranges::transform(out, out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        std::string NormalizeTexturePath(std::string_view rawPath) {
            std::string path = ToLowerAscii(rawPath);
            std::ranges::replace(path, '/', '\\');
            while (!path.empty() && path.front() == '\\') {
                path.erase(path.begin());
            }
            if (!path.starts_with("textures\\")) {
                path = "textures\\" + path;
            }
            return path;
        }

        std::string TrimWhitespace(std::string_view sv) {
            const auto first = sv.find_first_not_of(" \t\r\n\"'");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = sv.find_last_not_of(" \t\r\n\"'");
            return std::string(sv.substr(first, last - first + 1));
        }

        bool ReadVfsTextureToBytes(std::string_view normalizedPath, std::vector<std::uint8_t>& outBytes) {
            RE::BSResourceNiBinaryStream stream(std::string{normalizedPath});
            if (!stream.good()) {
                return false;
            }

            RE::NiBinaryStream::BufferInfo bufInfo{};
            stream.get_info(bufInfo);
            if (bufInfo.totalSize > 0) {
                outBytes.resize(bufInfo.totalSize);
                if (stream.read(reinterpret_cast<char*>(outBytes.data()), bufInfo.totalSize)) {
                    return true;
                }
                outBytes.clear();
            }

            if (!stream.stream) {
                return false;
            }

            constexpr std::size_t kChunkSize = 65536;
            std::array<std::uint8_t, kChunkSize> chunk{};
            while (true) {
                std::uint64_t bytesRead = 0;
                const auto err = stream.stream->DoRead(chunk.data(), kChunkSize, bytesRead);
                if (bytesRead > 0) {
                    outBytes.insert(
                        outBytes.end(),
                        chunk.begin(),
                        chunk.begin() + static_cast<std::ptrdiff_t>(bytesRead));
                }
                if (err != RE::BSResource::ErrorCode::kNone || bytesRead < kChunkSize) {
                    break;
                }
            }

            return !outBytes.empty();
        }

        RE::NiPoint3 SampleNormalPixel(
            const DirectX::Image& img,
            std::size_t x,
            std::size_t y,
            bool isBGRA,
            bool reconstructZ) {
            const std::uint8_t* row = img.pixels + y * img.rowPitch;
            const std::uint8_t* px = row + x * 4;

            const std::uint8_t rByte = isBGRA ? px[2] : px[0];
            const std::uint8_t gByte = px[1];
            const std::uint8_t bByte = isBGRA ? px[0] : px[2];

            const float nx = (static_cast<float>(rByte) / 255.0f) * 2.0f - 1.0f;
            const float ny = (static_cast<float>(gByte) / 255.0f) * 2.0f - 1.0f;
            float nz = (static_cast<float>(bByte) / 255.0f) * 2.0f - 1.0f;

            if (reconstructZ || bByte == 0) {
                nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
            }

            return RE::NiPoint3(nx, ny, nz);
        }

        bool IsValidNormalVector(const RE::NiPoint3& n) {
            const float lenSq = n.x * n.x + n.y * n.y + n.z * n.z;
            return lenSq >= 0.25f && lenSq <= 2.25f;
        }

        float DistSq(const RE::NiPoint3& a, const RE::NiPoint3& b) {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz;
        }

        float ComputePatchLaplacianEnergy(
            const DirectX::Image& img,
            const UVRect& uvRect,
            bool isBGRA,
            bool reconstructZ) {
            if (img.width < 8 || img.height < 8 || !img.pixels) {
                return 0.0f;
            }

            const auto xMin = static_cast<std::size_t>(
                std::clamp(uvRect.uMin, 0.02f, 0.96f) * static_cast<float>(img.width));
            const auto xMax = static_cast<std::size_t>(
                std::clamp(uvRect.uMax, 0.04f, 0.98f) * static_cast<float>(img.width));
            const auto yMin = static_cast<std::size_t>(
                std::clamp(uvRect.vMin, 0.02f, 0.96f) * static_cast<float>(img.height));
            const auto yMax = static_cast<std::size_t>(
                std::clamp(uvRect.vMax, 0.04f, 0.98f) * static_cast<float>(img.height));

            const std::size_t xStart = std::max<std::size_t>(1, std::min(xMin, img.width - 2));
            const std::size_t xEnd = std::min<std::size_t>(img.width - 2, std::max(xMax, xStart + 1));
            const std::size_t yStart = std::max<std::size_t>(1, std::min(yMin, img.height - 2));
            const std::size_t yEnd = std::min<std::size_t>(img.height - 2, std::max(yMax, yStart + 1));

            // Single-pixel step threshold squared (0.28^2 = 0.0784) to reject UV island seams & navel holes
            constexpr float kMaxStepDistSq = 0.0784f;

            double energySum = 0.0;
            std::size_t sampleCount = 0;

            for (std::size_t y = yStart; y <= yEnd; ++y) {
                for (std::size_t x = xStart; x <= xEnd; ++x) {
                    const RE::NiPoint3 nCenter = SampleNormalPixel(img, x, y, isBGRA, reconstructZ);
                    const RE::NiPoint3 nLeft = SampleNormalPixel(img, x - 1, y, isBGRA, reconstructZ);
                    const RE::NiPoint3 nRight = SampleNormalPixel(img, x + 1, y, isBGRA, reconstructZ);
                    const RE::NiPoint3 nUp = SampleNormalPixel(img, x, y - 1, isBGRA, reconstructZ);
                    const RE::NiPoint3 nDown = SampleNormalPixel(img, x, y + 1, isBGRA, reconstructZ);

                    if (!IsValidNormalVector(nCenter) || !IsValidNormalVector(nLeft) ||
                        !IsValidNormalVector(nRight) || !IsValidNormalVector(nUp) ||
                        !IsValidNormalVector(nDown)) {
                        continue;
                    }

                    if (DistSq(nCenter, nLeft) > kMaxStepDistSq ||
                        DistSq(nCenter, nRight) > kMaxStepDistSq ||
                        DistSq(nCenter, nUp) > kMaxStepDistSq ||
                        DistSq(nCenter, nDown) > kMaxStepDistSq) {
                        continue;
                    }

                    const RE::NiPoint3 laplacian =
                        nCenter - (nLeft + nRight + nUp + nDown) * 0.25f;

                    const float ePixel = std::sqrt(
                        1.2f * laplacian.x * laplacian.x +
                        1.5f * laplacian.z * laplacian.z +
                        0.5f * laplacian.y * laplacian.y);
                    energySum += static_cast<double>(ePixel);
                    ++sampleCount;
                }
            }

            if (sampleCount == 0) {
                return 0.0f;
            }
            return static_cast<float>(energySum / static_cast<double>(sampleCount));
        }

    }  // namespace

    NormalMapAnalyzer& NormalMapAnalyzer::GetInstance() {
        static NormalMapAnalyzer instance;
        return instance;
    }

    void NormalMapAnalyzer::LoadConfig(const std::filesystem::path& dataRoot) {
        const auto iniPath = dataRoot / "SKSE" / "Plugins" / "BodyLanguage.ini";
        std::ifstream file(iniPath);
        if (!file.is_open()) {
            spdlog::info(
                "[NormalMapAnalyzer] BodyLanguage.ini not found; using defaults (Smooth={:.4f}, Ripped={:.4f}).",
                m_smoothBaseline,
                m_rippedBaseline);
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            const auto commentPos = line.find_first_of(";#");
            if (commentPos != std::string::npos) {
                line.erase(commentPos);
            }
            const auto eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                continue;
            }

            const std::string key = ToLowerAscii(TrimWhitespace(std::string_view(line).substr(0, eqPos)));
            const std::string valStr = TrimWhitespace(std::string_view(line).substr(eqPos + 1));
            if (valStr.empty()) {
                continue;
            }

            try {
                const float val = std::stof(valStr);
                if (key == "smoothbaseline" && val >= 0.0f && val < 1.0f) {
                    m_smoothBaseline = val;
                } else if (key == "rippedbaseline" && val > 0.001f && val <= 1.0f) {
                    m_rippedBaseline = val;
                }
            } catch (const std::exception&) {
                continue;
            }
        }

        if (m_rippedBaseline <= m_smoothBaseline + 0.001f) {
            m_rippedBaseline = m_smoothBaseline + 0.030f;
        }

        spdlog::info(
            "[NormalMapAnalyzer] Configured baselines: SmoothBaseline={:.4f}, RippedBaseline={:.4f}",
            m_smoothBaseline,
            m_rippedBaseline);
    }

    std::string NormalMapAnalyzer::ResolveActiveNormalMapPath(
        RE::Actor* actor,
        RE::TESObjectARMA* nakedArma,
        bool isFemale,
        std::string_view fallbackNifMsn) const {
        if (actor) {
            if (auto* root = actor->Get3D(false)) {
                std::string sceneMsn;
                RE::BSVisit::TraverseScenegraphGeometries(
                    root,
                    [&](RE::BSGeometry* geo) -> RE::BSVisit::BSVisitControl {
                        if (!geo) {
                            return RE::BSVisit::BSVisitControl::kContinue;
                        }
                        auto* shaderProp = geo->lightingShaderProp_cast();
                        if (!shaderProp) {
                            return RE::BSVisit::BSVisitControl::kContinue;
                        }
                        auto* mat = static_cast<RE::BSLightingShaderMaterialBase*>(
                            shaderProp->GetBaseMaterial());
                        if (!mat) {
                            return RE::BSVisit::BSVisitControl::kContinue;
                        }
                        auto texSet = mat->GetTextureSet();
                        if (!texSet) {
                            return RE::BSVisit::BSVisitControl::kContinue;
                        }
                        const char* path = texSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal);
                        if (!path || *path == '\0') {
                            return RE::BSVisit::BSVisitControl::kContinue;
                        }

                        const std::string fname = ToLowerAscii(
                            std::filesystem::path(path).filename().string());
                        if (fname.find("body") != std::string::npos &&
                            fname.find("msn") != std::string::npos) {
                            sceneMsn = path;
                            return RE::BSVisit::BSVisitControl::kStop;
                        }
                        return RE::BSVisit::BSVisitControl::kContinue;
                    });

                if (!sceneMsn.empty()) {
                    return sceneMsn;
                }
            }
        }

        if (nakedArma) {
            const auto sexIdx = isFemale ? RE::SEXES::kFemale : RE::SEXES::kMale;
            if (auto* skinTexSet = nakedArma->skinTextures[sexIdx]) {
                const char* path = skinTexSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal);
                if (path && *path != '\0') {
                    return std::string(path);
                }
            }
        }

        return std::string(fallbackNifMsn);
    }

    float NormalMapAnalyzer::EvaluateNormalMapTone(
        std::string_view msnPath,
        const UVRect& abdomenUV,
        const UVRect& deltoidUV) {
        if (msnPath.empty()) {
            return 0.25f;
        }

        const std::string normPath = NormalizeTexturePath(msnPath);

        {
            std::shared_lock readLock(m_cacheMutex);
            auto it = m_scoreCache.find(normPath);
            if (it != m_scoreCache.end()) {
                spdlog::info(
                    "[NormalMapAnalyzer] Cache hit for '{}' -> S_tex_tone={:.3f}",
                    normPath,
                    it->second);
                return it->second;
            }
        }

        std::vector<std::uint8_t> ddsBytes;
        if (!ReadVfsTextureToBytes(normPath, ddsBytes) || ddsBytes.size() < 128) {
            spdlog::warn("[NormalMapAnalyzer] Could not read DDS stream '{}' (fallback S_tex_tone=0.25)", normPath);
            return 0.25f;
        }

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage scratchImg;
        HRESULT hr = DirectX::LoadFromDDSMemory(
            ddsBytes.data(),
            ddsBytes.size(),
            DirectX::DDS_FLAGS_NONE,
            &metadata,
            scratchImg);
        if (FAILED(hr) || scratchImg.GetImageCount() == 0) {
            spdlog::warn("[NormalMapAnalyzer] DirectXTex failed to load '{}' (hr=0x{:08X})", normPath, static_cast<std::uint32_t>(hr));
            return 0.25f;
        }

        // Select mip level closest to 256x256 (or 512x512 if no smaller mip >= 128 exists)
        std::size_t targetMip = 0;
        for (std::size_t m = 0; m < metadata.mipLevels; ++m) {
            const DirectX::Image* candidate = scratchImg.GetImage(m, 0, 0);
            if (!candidate) {
                break;
            }
            const std::size_t minDim = std::min(candidate->width, candidate->height);
            if (minDim < 128) {
                break;
            }
            targetMip = m;
            if (minDim <= 256) {
                break;
            }
        }

        const DirectX::Image* mipImg = scratchImg.GetImage(targetMip, 0, 0);
        if (!mipImg) {
            return 0.25f;
        }

        const auto origFormat = static_cast<std::uint32_t>(mipImg->format);
        const bool isBC5 =
            (mipImg->format == DXGI_FORMAT_BC5_UNORM) ||
            (mipImg->format == DXGI_FORMAT_BC5_SNORM) ||
            (mipImg->format == DXGI_FORMAT_BC5_TYPELESS);

        DirectX::ScratchImage decodedImg;
        if (DirectX::IsCompressed(mipImg->format)) {
            hr = DirectX::Decompress(*mipImg, DXGI_FORMAT_R8G8B8A8_UNORM, decodedImg);
            if (FAILED(hr)) {
                spdlog::warn("[NormalMapAnalyzer] Failed to decompress mip {} of '{}'", targetMip, normPath);
                return 0.25f;
            }
            mipImg = decodedImg.GetImage(0, 0, 0);
        } else if (
            mipImg->format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            mipImg->format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
            mipImg->format != DXGI_FORMAT_B8G8R8A8_UNORM &&
            mipImg->format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
            hr = DirectX::Convert(
                *mipImg,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DirectX::TEX_FILTER_DEFAULT,
                DirectX::TEX_THRESHOLD_DEFAULT,
                decodedImg);
            if (FAILED(hr)) {
                return 0.25f;
            }
            mipImg = decodedImg.GetImage(0, 0, 0);
        }

        if (!mipImg || !mipImg->pixels) {
            return 0.25f;
        }

        const bool isBGRA =
            (mipImg->format == DXGI_FORMAT_B8G8R8A8_UNORM) ||
            (mipImg->format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);

        const float abEnergy = ComputePatchLaplacianEnergy(*mipImg, abdomenUV, isBGRA, isBC5);
        const float deltEnergy = ComputePatchLaplacianEnergy(*mipImg, deltoidUV, isBGRA, isBC5);
        const float combinedEnergy = 0.75f * abEnergy + 0.25f * deltEnergy;

        const float denom = std::max(0.001f, m_rippedBaseline - m_smoothBaseline);
        const float texToneScore = std::clamp(
            (combinedEnergy - m_smoothBaseline) / denom,
            0.0f,
            1.0f);

        {
            std::unique_lock writeLock(m_cacheMutex);
            m_scoreCache[normPath] = texToneScore;
        }

        spdlog::info(
            "[NormalMapAnalyzer] Decoded '{}' (base={}x{}, mip={} [{}x{}], dxgiFmt={}): "
            "abEnergy={:.5f} deltEnergy={:.5f} combined={:.5f} -> S_tex_tone={:.3f}",
            normPath,
            metadata.width,
            metadata.height,
            targetMip,
            mipImg->width,
            mipImg->height,
            origFormat,
            abEnergy,
            deltEnergy,
            combinedEnergy,
            texToneScore);

        return texToneScore;
    }

}  // namespace BodyLanguage

