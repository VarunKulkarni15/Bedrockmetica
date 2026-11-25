#include "UiAssets.hpp"

#include <wincodec.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace UiAssets {

namespace {

struct RGBA {
    uint8_t r, g, b, a;
};

constexpr RGBA MakeCol(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return { r, g, b, a };
}

RGBA Shade(RGBA c, float factor) {
    auto clampU8 = [](int v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v, 0, 255));
    };
    return {
        clampU8(static_cast<int>(c.r * factor)),
        clampU8(static_cast<int>(c.g * factor)),
        clampU8(static_cast<int>(c.b * factor)),
        c.a
    };
}

void PutPixel(std::vector<uint8_t>& buf, int w, int h, int x, int y, RGBA c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    size_t idx = (static_cast<size_t>(y) * w + x) * 4;
    if (c.a == 255) {
        buf[idx + 0] = c.r;
        buf[idx + 1] = c.g;
        buf[idx + 2] = c.b;
        buf[idx + 3] = 255;
    } else if (c.a > 0) {
        float sa = c.a / 255.0f;
        float da = buf[idx + 3] / 255.0f;
        float outA = sa + da * (1.0f - sa);
        if (outA > 0.0001f) {
            buf[idx + 0] = static_cast<uint8_t>((c.r * sa + buf[idx + 0] * da * (1.0f - sa)) / outA);
            buf[idx + 1] = static_cast<uint8_t>((c.g * sa + buf[idx + 1] * da * (1.0f - sa)) / outA);
            buf[idx + 2] = static_cast<uint8_t>((c.b * sa + buf[idx + 2] * da * (1.0f - sa)) / outA);
            buf[idx + 3] = static_cast<uint8_t>(outA * 255.0f);
        }
    }
}

struct PlateSpec {
    RGBA border;
    RGBA top;
    RGBA bottom;
    RGBA highlight;
    RGBA darkRim;
    int chamfer;
    int borderW;
    bool twoTone;
};

int ChamferInsetAt(int x, int y, int w, int h, int c) {
    int dx = std::min(x, w - 1 - x);
    int dy = std::min(y, h - 1 - y);
    int minEdge = std::min(dx, dy);
    int diag = dx + dy - c;
    return std::min(minEdge, diag);
}

std::vector<uint8_t> BuildPlateSprite(const PlateSpec& spec) {
    const int S = kPlateTexSize;
    std::vector<uint8_t> rgba(S * S * 4, 0);

    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            int outerDist = ChamferInsetAt(x, y, S, S, spec.chamfer);
            if (outerDist < 0) continue;

            if (outerDist < spec.borderW) {
                PutPixel(rgba, S, S, x, y, spec.border);
                continue;
            }

            RGBA body = (spec.twoTone && y < S / 2) ? spec.top : (spec.twoTone ? spec.bottom : spec.top);

            if (y >= spec.borderW && y < spec.borderW + 3 && x >= spec.chamfer + 2 && x <= S - 1 - spec.chamfer - 2) {
                body = spec.highlight;
            } else if (y <= S - 1 - spec.borderW && y > S - 1 - spec.borderW - 4 && x >= spec.chamfer + 2 && x <= S - 1 - spec.chamfer - 2) {
                body = spec.darkRim;
            }
            PutPixel(rgba, S, S, x, y, body);
        }
    }
    return rgba;
}

const PlateSpec& GetPlateSpec(PlateId id) {
    static const std::array<PlateSpec, static_cast<size_t>(PlateId::Count)> specs = {{
        { MakeCol(10, 12, 18),  MakeCol(26, 31, 45, 250), MakeCol(20, 24, 36, 250), MakeCol(72, 84, 112),  MakeCol(12, 15, 22),  7, 4, false },
        { MakeCol(104, 62, 4),  MakeCol(252, 198, 56),    MakeCol(236, 160, 24),    MakeCol(255, 236, 148),MakeCol(170, 108, 12),6, 3, true  },
        { MakeCol(10, 54, 102), MakeCol(56, 166, 240),    MakeCol(30, 134, 214),    MakeCol(156, 226, 255),MakeCol(16, 82, 146), 6, 3, true  },
        { MakeCol(12, 15, 22),  MakeCol(32, 38, 54),      MakeCol(24, 29, 42),      MakeCol(66, 78, 108),  MakeCol(16, 19, 28),  6, 3, false },
        { MakeCol(14, 17, 25),  MakeCol(46, 54, 76),      MakeCol(34, 40, 58),      MakeCol(86, 100, 134), MakeCol(20, 24, 34),  5, 3, true  },
        { MakeCol(140, 86, 8),  MakeCol(56, 65, 92),      MakeCol(40, 46, 68),      MakeCol(252, 204, 72), MakeCol(178, 114, 14),5, 3, true  },
        { MakeCol(104, 62, 4),  MakeCol(216, 142, 18),    MakeCol(186, 114, 10),    MakeCol(255, 220, 112),MakeCol(132, 78, 6),  5, 3, true  },
        { MakeCol(14, 78, 34),  MakeCol(66, 220, 112),    MakeCol(38, 182, 82),     MakeCol(176, 255, 200),MakeCol(20, 116, 52), 6, 3, true  },
        { MakeCol(107, 18, 22), MakeCol(236, 70, 70),     MakeCol(212, 42, 46),     MakeCol(255, 164, 152),MakeCol(145, 24, 28), 5, 3, true  },
        { MakeCol(10, 12, 18),  MakeCol(14, 17, 25),      MakeCol(14, 17, 25),      MakeCol(36, 44, 62),   MakeCol(10, 12, 18),  4, 3, false },
        { MakeCol(22, 118, 54), MakeCol(30, 42, 38, 248), MakeCol(22, 32, 29, 248), MakeCol(86, 236, 136), MakeCol(20, 86, 44),  6, 3, true  },
        { MakeCol(22, 106, 176),MakeCol(26, 36, 52, 248), MakeCol(20, 28, 42, 248), MakeCol(110, 212, 255),MakeCol(14, 70, 122), 6, 3, true  },
        { MakeCol(152, 94, 12), MakeCol(42, 36, 24, 248), MakeCol(32, 27, 18, 248), MakeCol(255, 216, 102),MakeCol(128, 78, 10), 6, 3, true  },
        { MakeCol(156, 32, 38), MakeCol(46, 26, 30, 248), MakeCol(34, 20, 24, 248), MakeCol(255, 132, 136),MakeCol(122, 22, 26), 6, 3, true  },
    }};
    return specs[static_cast<size_t>(id)];
}

void FillRect(std::vector<uint8_t>& buf, int S, int x0, int y0, int x1, int y1, RGBA c) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            PutPixel(buf, S, S, x, y, c);
        }
    }
}

std::vector<uint8_t> BuildBadgeSprite(BadgeId id) {
    const int S = 36;
    std::vector<uint8_t> rgba(S * S * 4, 0);
    int cx = S / 2;
    int cy = S / 2;
    int rOuter = 16;
    int rInner = 12;

    RGBA bdr = MakeCol(10, 12, 18);
    RGBA bg  = MakeCol(24, 42, 68);
    if (id == BadgeId::Cube)  { bdr = MakeCol(104, 62, 4);  bg = MakeCol(18, 40, 68); }
    if (id == BadgeId::Check) { bdr = MakeCol(14, 78, 34);  bg = MakeCol(34, 168, 78); }
    if (id == BadgeId::Star)  { bdr = MakeCol(118, 70, 6);  bg = MakeCol(228, 152, 22); }
    if (id == BadgeId::Alert) { bdr = MakeCol(107, 18, 22); bg = MakeCol(208, 42, 48); }
    if (id == BadgeId::Info)  { bdr = MakeCol(10, 56, 106); bg = MakeCol(34, 138, 218); }

    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            int md = std::abs(x - cx) + std::abs(y - cy);
            if (md <= rOuter) {
                PutPixel(rgba, S, S, x, y, (md > rInner) ? bdr : bg);
            }
        }
    }

    RGBA white = MakeCol(255, 255, 255);
    RGBA cyanTop = MakeCol(215, 250, 255);
    RGBA cyanL = MakeCol(92, 218, 255);
    RGBA cyanR = MakeCol(38, 165, 224);

    if (id == BadgeId::Cube) {
        for (int y = cy - 7; y <= cy + 7; ++y) {
            for (int x = cx - 7; x <= cx + 7; ++x) {
                int dx = x - cx;
                int dy = y - cy;
                if (std::abs(dx) + std::abs(dy * 2) <= 7 && dy <= 0) {
                    PutPixel(rgba, S, S, x, y, cyanTop);
                } else if (dx >= -6 && dx < 0 && dy >= 0 && dy <= 6 - (-dx) / 2 && std::abs(dx) <= 6) {
                    PutPixel(rgba, S, S, x, y, cyanL);
                } else if (dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 - dx / 2) {
                    PutPixel(rgba, S, S, x, y, cyanR);
                }
            }
        }
    } else if (id == BadgeId::Check) {
        for (int i = 0; i < 4; ++i) {
            FillRect(rgba, S, cx - 6 + i, cy + i, cx - 5 + i, cy + 2 + i, white);
        }
        for (int i = 0; i < 7; ++i) {
            FillRect(rgba, S, cx - 2 + i, cy + 3 - i, cx - 1 + i, cy + 5 - i, white);
        }
    } else if (id == BadgeId::Star) {
        for (int y = cy - 8; y <= cy + 8; ++y) {
            for (int x = cx - 8; x <= cx + 8; ++x) {
                int ax = std::abs(x - cx);
                int ay = std::abs(y - cy);
                if (ax <= 1 || ay <= 1 || ax + ay <= 4) {
                    PutPixel(rgba, S, S, x, y, white);
                }
            }
        }
    } else if (id == BadgeId::Alert) {
        FillRect(rgba, S, cx - 1, cy - 6, cx + 1, cy + 1, white);
        FillRect(rgba, S, cx - 1, cy + 4, cx + 1, cy + 6, white);
    } else if (id == BadgeId::Info) {
        FillRect(rgba, S, cx - 1, cy - 6, cx + 1, cy - 4, white);
        FillRect(rgba, S, cx - 1, cy - 2, cx + 1, cy + 6, white);
    }
    return rgba;
}

void DrawIsoMiniCube(std::vector<uint8_t>& rgba, int S, int cx, int cy, RGBA base) {
    const int dx = 4;
    const int dy = 2;
    const int h = 5;
    RGBA topC = Shade(base, 1.15f);
    RGBA leftC = Shade(base, 0.85f);
    RGBA rightC = Shade(base, 0.65f);

    for (int py = -dy; py <= dy + h; ++py) {
        for (int px = -dx; px <= dx; ++px) {
            int ix = cx + px;
            int iy = cy - h + py;
            if (py <= dy && (std::abs(px) * dy + std::abs(py) * dx) <= dx * dy) {
                PutPixel(rgba, S, S, ix, iy, topC);
            } else if (px < 0 && py > 0 && py <= h + dy - (-px) / 2 && (-px) <= dx) {
                PutPixel(rgba, S, S, ix, iy, leftC);
            } else if (px >= 0 && py > 0 && py <= h + dy - px / 2 && px <= dx) {
                PutPixel(rgba, S, S, ix, iy, rightC);
            }
        }
    }
}

std::vector<uint8_t> BuildThumbSprite(ThumbId id) {
    const int S = 64;
    std::vector<uint8_t> rgba(S * S * 4, 0);
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            int d = ChamferInsetAt(x, y, S, S, 4);
            if (d < 0) continue;
            PutPixel(rgba, S, S, x, y, (d < 2) ? MakeCol(56, 66, 92) : MakeCol(13, 17, 25));
        }
    }

    int cx = S / 2;
    int cy = 44;
    auto put = [&](int vx, int vy, int vz, RGBA col) {
        int px = cx + (vx - vz) * 4;
        int py = cy + (vx + vz) * 2 - vy * 5;
        DrawIsoMiniCube(rgba, S, px, py, col);
    };

    if (id == ThumbId::Pagoda) {
        for (int x = -1; x <= 1; ++x)
            for (int z = -1; z <= 1; ++z) {
                put(x, 0, z, MakeCol(235, 224, 204));
                put(x, 1, z, MakeCol(206, 48, 56));
            }
        put(0, 2, 0, MakeCol(248, 192, 46));
        put(-2, 1, 1, MakeCol(248, 148, 182));
    } else if (id == ThumbId::TradingHall) {
        for (int x = -2; x <= 2; ++x)
            for (int z = -1; z <= 1; ++z)
                put(x, 0, z, MakeCol(132, 142, 158));
        for (int x = -1; x <= 1; ++x)
            put(x, 1, 0, MakeCol(186, 134, 78));
    } else if (id == ThumbId::Bridge) {
        for (int z = -2; z <= 2; ++z) {
            put(0, 0, z, MakeCol(206, 56, 56));
            put(1, 0, z, MakeCol(226, 232, 240));
        }
    } else {
        for (int x = -1; x <= 1; ++x)
            for (int z = -1; z <= 1; ++z)
                put(x, 0, z, MakeCol(76, 84, 102));
        put(0, 1, 0, MakeCol(234, 60, 60));
    }
    return rgba;
}

struct BrarchiveEntry {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct BrarchiveFile {
    std::filesystem::path path;
    uint64_t dataStart = 0;
    std::vector<BrarchiveEntry> entries;
};

static std::vector<std::filesystem::path> FindVanillaPackDirs() {
    std::vector<std::filesystem::path> packs;
    wchar_t exeBuf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
    if (len == 0) return packs;

    std::error_code ec;
    std::filesystem::path rpDir = std::filesystem::path(exeBuf).parent_path() / L"data" / L"resource_packs";
    if (!std::filesystem::exists(rpDir, ec)) return packs;

    for (const auto& entry : std::filesystem::directory_iterator(rpDir, ec)) {
        if (!entry.is_directory(ec)) continue;
        std::string fname = entry.path().filename().string();
        if (fname.rfind("vanilla", 0) == 0) {
            packs.push_back(entry.path());
        }
    }
    std::sort(packs.begin(), packs.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        std::string sa = a.filename().string();
        std::string sb = b.filename().string();
        bool aBase = (sa == "vanilla");
        bool bBase = (sb == "vanilla");
        if (aBase != bBase) return !aBase;
        return sa > sb;
    });
    return packs;
}

static bool LoadBrarchiveIndex(const std::filesystem::path& path, BrarchiveFile& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    uint8_t hdr[16] = {};
    if (!f.read(reinterpret_cast<char*>(hdr), 16)) return false;

    uint64_t magic = 0;
    uint32_t count = 0;
    uint32_t ver = 0;
    std::memcpy(&magic, hdr + 0, 8);
    std::memcpy(&count, hdr + 8, 4);
    std::memcpy(&ver, hdr + 12, 4);
    if (magic != 0x267052a0b125277dULL || count == 0 || count > 65536 || ver > 4) return false;

    std::vector<uint8_t> table(static_cast<size_t>(count) * 256);
    if (!f.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size()))) return false;

    out.path = path;
    out.dataStart = 16ULL + static_cast<uint64_t>(count) * 256ULL;
    out.entries.clear();
    out.entries.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* rec = table.data() + static_cast<size_t>(i) * 256;
        uint8_t nameLen = rec[0];
        if (nameLen == 0 || nameLen > 240) continue;
        BrarchiveEntry e;
        e.name.assign(reinterpret_cast<const char*>(rec + 1), nameLen);
        std::memcpy(&e.offset, rec + 248, 4);
        std::memcpy(&e.size, rec + 252, 4);
        out.entries.push_back(std::move(e));
    }
    return !out.entries.empty();
}

static std::vector<BrarchiveFile> CollectVanillaArchives(const std::vector<std::filesystem::path>& packs) {
    std::vector<BrarchiveFile> archives;
    std::error_code ec;
    for (const auto& pack : packs) {
        std::filesystem::path texDir = pack / L"__brarchive" / L"textures";
        if (!std::filesystem::exists(texDir, ec)) continue;

        std::filesystem::path mainArc = texDir / L"blocks.brarchive";
        if (std::filesystem::exists(mainArc, ec)) {
            BrarchiveFile bf;
            if (LoadBrarchiveIndex(mainArc, bf)) archives.push_back(std::move(bf));
        }

        std::filesystem::path subBlocks = texDir / L"blocks";
        if (std::filesystem::exists(subBlocks, ec)) {
            for (const auto& f : std::filesystem::directory_iterator(subBlocks, ec)) {
                if (f.is_regular_file(ec) && f.path().extension() == L".brarchive") {
                    BrarchiveFile bf;
                    if (LoadBrarchiveIndex(f.path(), bf)) archives.push_back(std::move(bf));
                }
            }
        }
    }
    return archives;
}

static bool ReadTextureBytesFromVanilla(const std::vector<BrarchiveFile>& archives,
                                        const std::vector<std::filesystem::path>& packs,
                                        const std::vector<const char*>& candidates,
                                        std::vector<uint8_t>& outBytes,
                                        std::string& outName) {
    for (const char* name : candidates) {
        for (const auto& arc : archives) {
            for (const auto& e : arc.entries) {
                if (e.name == name && e.size > 0 && e.size < 4 * 1024 * 1024) {
                    std::ifstream f(arc.path, std::ios::binary);
                    if (!f.is_open()) continue;
                    f.seekg(static_cast<std::streamoff>(arc.dataStart + e.offset), std::ios::beg);
                    outBytes.resize(e.size);
                    if (f.read(reinterpret_cast<char*>(outBytes.data()), e.size)) {
                        outName = name;
                        return true;
                    }
                    outBytes.clear();
                }
            }
        }
        std::error_code ec;
        for (const auto& pack : packs) {
            std::filesystem::path loose = pack / L"textures" / L"blocks" / name;
            if (std::filesystem::exists(loose, ec)) {
                std::ifstream f(loose, std::ios::binary | std::ios::ate);
                if (!f.is_open()) continue;
                auto sz = f.tellg();
                if (sz > 0 && sz < 4 * 1024 * 1024) {
                    f.seekg(0, std::ios::beg);
                    outBytes.resize(static_cast<size_t>(sz));
                    if (f.read(reinterpret_cast<char*>(outBytes.data()), sz)) {
                        outName = name;
                        return true;
                    }
                    outBytes.clear();
                }
            }
        }
    }
    return false;
}

static bool DecodeImageMemoryToRGBA(IWICImagingFactory* wic,
                                    const std::vector<uint8_t>& raw,
                                    int& outW,
                                    int& outH,
                                    std::vector<uint8_t>& outRgba) {
    if (raw.size() >= 8 && raw[0] == 0x89 && raw[1] == 'P' && raw[2] == 'N' && raw[3] == 'G') {
        if (!wic) return false;
        IWICStream* stream = nullptr;
        if (FAILED(wic->CreateStream(&stream))) return false;
        HRESULT hr = stream->InitializeFromMemory(const_cast<BYTE*>(raw.data()), static_cast<DWORD>(raw.size()));
        IWICBitmapDecoder* decoder = nullptr;
        if (SUCCEEDED(hr)) {
            hr = wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        }
        IWICBitmapFrameDecode* frame = nullptr;
        if (SUCCEEDED(hr)) {
            hr = decoder->GetFrame(0, &frame);
        }
        IWICBitmapSource* rgbaSrc = nullptr;
        UINT w = 0, h = 0;
        if (SUCCEEDED(hr)) {
            hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame, &rgbaSrc);
        }
        if (SUCCEEDED(hr) && SUCCEEDED(rgbaSrc->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 2048 && h <= 4096) {
            outW = static_cast<int>(w);
            outH = static_cast<int>(h);
            outRgba.resize(static_cast<size_t>(w) * h * 4);
            hr = rgbaSrc->CopyPixels(nullptr, w * 4, static_cast<UINT>(outRgba.size()), outRgba.data());
        } else {
            hr = E_FAIL;
        }
        if (rgbaSrc) rgbaSrc->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        stream->Release();
        return SUCCEEDED(hr);
    }

    if (raw.size() >= 18 && raw[2] == 2) {
        uint16_t idLen = raw[0];
        uint16_t w = static_cast<uint16_t>(raw[12]) | (static_cast<uint16_t>(raw[13]) << 8);
        uint16_t h = static_cast<uint16_t>(raw[14]) | (static_cast<uint16_t>(raw[15]) << 8);
        uint8_t bpp = raw[16];
        uint8_t desc = raw[17];
        if (w == 0 || h == 0 || (bpp != 24 && bpp != 32)) return false;
        size_t hdrOff = 18 + idLen;
        size_t stride = (bpp / 8);
        if (raw.size() < hdrOff + static_cast<size_t>(w) * h * stride) return false;
        bool topOrigin = (desc & 0x20) != 0;
        outW = w;
        outH = h;
        outRgba.resize(static_cast<size_t>(w) * h * 4);
        const uint8_t* src = raw.data() + hdrOff;
        for (int y = 0; y < h; ++y) {
            int sy = topOrigin ? y : (h - 1 - y);
            for (int x = 0; x < w; ++x) {
                const uint8_t* sp = src + (static_cast<size_t>(sy) * w + x) * stride;
                uint8_t* dp = outRgba.data() + (static_cast<size_t>(y) * w + x) * 4;
                dp[0] = sp[2];
                dp[1] = sp[1];
                dp[2] = sp[0];
                dp[3] = (bpp == 32) ? sp[3] : 255;
            }
        }
        return true;
    }

    return false;
}

std::vector<const char*> GetVanillaCandidates(BlockIconId id) {
    switch (id) {
    case BlockIconId::StoneBricks:     return { "stonebrick.png" };
    case BlockIconId::SprucePlanks:    return { "planks_spruce.png" };
    case BlockIconId::RedNetherBricks: return { "red_nether_brick.png" };
    case BlockIconId::Calcite:         return { "calcite.png" };
    case BlockIconId::CherryLeaves:    return { "cherry_leaves_opaque.png", "cherry_leaves.png" };
    case BlockIconId::Lantern:         return { "lantern.png" };
    case BlockIconId::GoldBlock:       return { "gold_block.png" };
    case BlockIconId::OakPlanks:       return { "planks_oak.png" };
    case BlockIconId::OakFence:        return { "planks_oak.png" };
    case BlockIconId::MossCarpet:      return { "moss_block.png" };
    case BlockIconId::GlassPane:       return { "glass.png" };
    case BlockIconId::Lectern:         return { "lectern_top.png", "lectern_front.png" };
    case BlockIconId::SeaLantern:      return { "sea_lantern.png" };
    case BlockIconId::Hopper:          return { "hopper_outside.png", "hopper_top.png" };
    case BlockIconId::QuartzPillar:    return { "quartz_block_side.png" };
    case BlockIconId::RedTerracotta:   return { "hardened_clay_stained_red.png" };
    case BlockIconId::DarkOakStair:    return { "planks_big_oak.png" };
    case BlockIconId::Glowstone:       return { "glowstone.png" };
    case BlockIconId::DeepslateTiles:  return { "deepslate_tiles.png" };
    case BlockIconId::RedstoneBlock:   return { "redstone_block.png" };
    case BlockIconId::StickyPiston:    return { "piston_top_sticky.png" };
    case BlockIconId::Observer:        return { "observer_front.png" };
    case BlockIconId::CopperGrate:     return { "copper_grate.png" };
    default:                           return { "stone.png" };
    }
}

std::vector<uint8_t> ExtractVanillaBlockSprite(BlockIconId id,
                                               const std::vector<BrarchiveFile>& archives,
                                               const std::vector<std::filesystem::path>& packs,
                                               IWICImagingFactory* wic) {
    const int S = 32;
    std::vector<uint8_t> out(S * S * 4, 0);

    std::vector<uint8_t> raw;
    std::string matchedName;
    if (!ReadTextureBytesFromVanilla(archives, packs, GetVanillaCandidates(id), raw, matchedName)) {
        return out;
    }

    int srcW = 0, srcH = 0;
    std::vector<uint8_t> rgba;
    if (!DecodeImageMemoryToRGBA(wic, raw, srcW, srcH, rgba) || srcW <= 0 || srcH <= 0) {
        return out;
    }

    int frameH = (srcH > srcW && (srcH % srcW) == 0) ? srcW : srcH;
    for (int y = 0; y < S; ++y) {
        int sy = std::min(frameH - 1, (y * frameH) / S);
        for (int x = 0; x < S; ++x) {
            int sx = std::min(srcW - 1, (x * srcW) / S);
            const uint8_t* sp = rgba.data() + (static_cast<size_t>(sy) * srcW + sx) * 4;
            uint8_t* dp = out.data() + (static_cast<size_t>(y) * S + x) * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
    return out;
}

struct SpriteEntry {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    ID3D11ShaderResourceView* dx11Srv = nullptr;
    ID3D12Resource* dx12Tex = nullptr;
    ImTextureID texId = 0;
};

static std::vector<SpriteEntry> g_Sprites;
static bool g_Initialized = false;
static bool g_Ready = false;

void BuildCpuSpritesOnce() {
    if (g_Initialized) return;
    g_Initialized = true;

    const size_t total =
        static_cast<size_t>(PlateId::Count) +
        static_cast<size_t>(BadgeId::Count) +
        static_cast<size_t>(ThumbId::Count) +
        static_cast<size_t>(BlockIconId::Count);
    g_Sprites.resize(total);

    size_t idx = 0;
    for (size_t i = 0; i < static_cast<size_t>(PlateId::Count); ++i, ++idx) {
        g_Sprites[idx].width = kPlateTexSize;
        g_Sprites[idx].height = kPlateTexSize;
        g_Sprites[idx].rgba = BuildPlateSprite(GetPlateSpec(static_cast<PlateId>(i)));
    }
    for (size_t i = 0; i < static_cast<size_t>(BadgeId::Count); ++i, ++idx) {
        g_Sprites[idx].width = 36;
        g_Sprites[idx].height = 36;
        g_Sprites[idx].rgba = BuildBadgeSprite(static_cast<BadgeId>(i));
    }
    for (size_t i = 0; i < static_cast<size_t>(ThumbId::Count); ++i, ++idx) {
        g_Sprites[idx].width = 64;
        g_Sprites[idx].height = 64;
        g_Sprites[idx].rgba = BuildThumbSprite(static_cast<ThumbId>(i));
    }

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comOwned = (comHr == S_OK || comHr == S_FALSE);
    IWICImagingFactory* wic = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));

    auto packs = FindVanillaPackDirs();
    auto archives = CollectVanillaArchives(packs);

    for (size_t i = 0; i < static_cast<size_t>(BlockIconId::Count); ++i, ++idx) {
        g_Sprites[idx].width = 32;
        g_Sprites[idx].height = 32;
        g_Sprites[idx].rgba = ExtractVanillaBlockSprite(static_cast<BlockIconId>(i), archives, packs, wic);
    }

    if (wic) wic->Release();
    if (comOwned) CoUninitialize();
}

bool UploadOneDX11(ID3D11Device* dev, SpriteEntry& s) {
    if (s.dx11Srv) return true;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = s.width;
    desc.Height = s.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = s.rgba.data();
    init.SysMemPitch = s.width * 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&desc, &init, &tex))) return false;
    HRESULT hr = dev->CreateShaderResourceView(tex, nullptr, &s.dx11Srv);
    tex->Release();
    if (FAILED(hr) || !s.dx11Srv) return false;
    s.texId = reinterpret_cast<ImTextureID>(s.dx11Srv);
    return true;
}

}

void EnsureDX11(ID3D11Device* device) {
    if (!device || g_Ready) return;
    BuildCpuSpritesOnce();

    bool allOk = true;
    for (auto& s : g_Sprites) {
        if (!UploadOneDX11(device, s)) allOk = false;
    }
    g_Ready = allOk;
}

void EnsureDX12(ID3D12Device* device, ID3D12CommandQueue* queue, SrvAllocFn allocFn) {
    if (!device || !queue || !allocFn || g_Ready) return;
    BuildCpuSpritesOnce();

    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* cmd = nullptr;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))) return;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&cmd)))) {
        alloc->Release();
        return;
    }

    std::vector<ID3D12Resource*> tempUploads;
    tempUploads.reserve(g_Sprites.size());

    for (auto& s : g_Sprites) {
        if (s.dx12Tex) continue;

        D3D12_HEAP_PROPERTIES defHeap = {};
        defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = s.width;
        texDesc.Height = s.height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;

        if (FAILED(device->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&s.dx12Tex)))) {
            continue;
        }

        UINT64 uploadSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
        UINT rows = 0;
        UINT64 rowBytes = 0;
        device->GetCopyableFootprints(&texDesc, 0, 1, 0, &fp, &rows, &rowBytes, &uploadSize);

        D3D12_HEAP_PROPERTIES upHeap = {};
        upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = uploadSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ID3D12Resource* uploadBuf = nullptr;
        if (FAILED(device->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(&uploadBuf)))) {
            s.dx12Tex->Release();
            s.dx12Tex = nullptr;
            continue;
        }

        void* mapped = nullptr;
        if (SUCCEEDED(uploadBuf->Map(0, nullptr, &mapped))) {
            uint8_t* dst = static_cast<uint8_t*>(mapped) + fp.Offset;
            for (UINT r = 0; r < rows; ++r) {
                std::memcpy(dst + r * fp.Footprint.RowPitch, s.rgba.data() + r * (s.width * 4), s.width * 4);
            }
            uploadBuf->Unmap(0, nullptr);
        }

        D3D12_TEXTURE_COPY_LOCATION dstLoc = { s.dx12Tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
        dstLoc.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION srcLoc = { uploadBuf, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {} };
        srcLoc.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = s.dx12Tex;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE cpuH = {};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuH = {};
        allocFn(&cpuH, &gpuH);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->CreateShaderResourceView(s.dx12Tex, &srv, cpuH);
        s.texId = static_cast<ImTextureID>(gpuH.ptr);

        tempUploads.push_back(uploadBuf);
    }

    cmd->Close();
    ID3D12CommandList* lists[] = { cmd };
    queue->ExecuteCommandLists(1, lists);

    ID3D12Fence* fence = nullptr;
    if (SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        HANDLE ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        queue->Signal(fence, 1);
        if (fence->GetCompletedValue() < 1 && ev) {
            fence->SetEventOnCompletion(1, ev);
            WaitForSingleObject(ev, 2000);
        }
        if (ev) CloseHandle(ev);
        fence->Release();
    }

    for (auto* u : tempUploads) u->Release();
    cmd->Release();
    alloc->Release();
    g_Ready = true;
}

void Shutdown() {
    for (auto& s : g_Sprites) {
        if (s.dx11Srv) { s.dx11Srv->Release(); s.dx11Srv = nullptr; }
        if (s.dx12Tex) { s.dx12Tex->Release(); s.dx12Tex = nullptr; }
        s.texId = 0;
    }
    g_Ready = false;
}

bool AreTexturesReady() { return g_Ready; }

ImTextureID GetPlateTex(PlateId id) {
    size_t idx = static_cast<size_t>(id);
    return idx < g_Sprites.size() ? g_Sprites[idx].texId : 0;
}

ImTextureID GetBadgeTex(BadgeId id) {
    size_t idx = static_cast<size_t>(PlateId::Count) + static_cast<size_t>(id);
    return idx < g_Sprites.size() ? g_Sprites[idx].texId : 0;
}

ImTextureID GetThumbTex(ThumbId id) {
    size_t idx = static_cast<size_t>(PlateId::Count) + static_cast<size_t>(BadgeId::Count) + static_cast<size_t>(id);
    return idx < g_Sprites.size() ? g_Sprites[idx].texId : 0;
}

ImTextureID GetBlockIconTex(BlockIconId id) {
    size_t idx = static_cast<size_t>(PlateId::Count) + static_cast<size_t>(BadgeId::Count) +
                 static_cast<size_t>(ThumbId::Count) + static_cast<size_t>(id);
    return idx < g_Sprites.size() ? g_Sprites[idx].texId : 0;
}

BlockIconId ResolveBlockIcon(const std::string& blockName) {
    if (blockName == "Stone Bricks")      return BlockIconId::StoneBricks;
    if (blockName == "Spruce Planks")     return BlockIconId::SprucePlanks;
    if (blockName == "Red Nether Bricks") return BlockIconId::RedNetherBricks;
    if (blockName == "Calcite")           return BlockIconId::Calcite;
    if (blockName == "Cherry Leaves")     return BlockIconId::CherryLeaves;
    if (blockName == "Lantern")           return BlockIconId::Lantern;
    if (blockName == "Gold Block")        return BlockIconId::GoldBlock;
    if (blockName == "Oak Planks")        return BlockIconId::OakPlanks;
    if (blockName == "Oak Fence")         return BlockIconId::OakFence;
    if (blockName == "Moss Carpet")       return BlockIconId::MossCarpet;
    if (blockName == "Glass Pane")        return BlockIconId::GlassPane;
    if (blockName == "Lectern")           return BlockIconId::Lectern;
    if (blockName == "Sea Lantern")       return BlockIconId::SeaLantern;
    if (blockName == "Hopper")            return BlockIconId::Hopper;
    if (blockName == "Quartz Pillar")     return BlockIconId::QuartzPillar;
    if (blockName == "Red Terracotta")    return BlockIconId::RedTerracotta;
    if (blockName == "Dark Oak Stair")    return BlockIconId::DarkOakStair;
    if (blockName == "Glowstone")         return BlockIconId::Glowstone;
    if (blockName == "Deepslate Tiles")   return BlockIconId::DeepslateTiles;
    if (blockName == "Redstone Block")    return BlockIconId::RedstoneBlock;
    if (blockName == "Sticky Piston")     return BlockIconId::StickyPiston;
    if (blockName == "Observer")          return BlockIconId::Observer;
    if (blockName == "Copper Grate")      return BlockIconId::CopperGrate;
    return BlockIconId::Generic;
}

}
