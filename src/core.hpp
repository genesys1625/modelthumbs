// core.hpp - Platformdan bağımsız çekirdek: zip/deflate, PNG, STL, 3MF, kontak sayfası, STL çizimi.
// Windows'a özel hiçbir şey içermez; Linux'ta test edilebilir.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mt {

// Opak BGRA görüntü (A her zaman 255)
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> bgra;
};

// ---------------------------------------------------------------- deflate
namespace detail {

struct Bits {
    const uint8_t* p;
    size_t n, pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool err = false;
    Bits(const uint8_t* p_, size_t n_) : p(p_), n(n_) {}
    int get(int need) {
        while (cnt < need) {
            if (pos >= n) { err = true; return 0; }
            buf |= (uint32_t)p[pos++] << cnt;
            cnt += 8;
        }
        int v = (int)(buf & ((1u << need) - 1));
        buf >>= need;
        cnt -= need;
        return v;
    }
};

struct Huff {
    short count[16];
    short symbol[320];
};

inline int huff_build(Huff& h, const short* len, int n) {
    for (int i = 0; i < 16; i++) h.count[i] = 0;
    for (int s = 0; s < n; s++) h.count[len[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l <= 15; l++) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0) return left;
    }
    short offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = (short)(offs[l] + h.count[l]);
    for (int s = 0; s < n; s++)
        if (len[s]) h.symbol[offs[len[s]]++] = (short)s;
    return left;
}

inline int huff_decode(Bits& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= 15; l++) {
        code |= b.get(1);
        int count = h.count[l];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -10;
}

}  // namespace detail

// Ham deflate akışını açar. max_out aşılırsa false döner.
inline bool inflate_raw(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t max_out) {
    using namespace detail;
    static const short LENS[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short DISTS[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    static const short ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    Bits b(in, n);
    int last;
    do {
        last = b.get(1);
        int type = b.get(2);
        if (b.err) return false;
        if (type == 0) {
            b.buf = 0;
            b.cnt = 0;
            if (b.pos + 4 > b.n) return false;
            unsigned len = b.p[b.pos] | (b.p[b.pos + 1] << 8);
            unsigned nlen = b.p[b.pos + 2] | (b.p[b.pos + 3] << 8);
            b.pos += 4;
            if ((len ^ 0xffffu) != nlen) return false;
            if (b.pos + len > b.n) return false;
            if (out.size() + len > max_out) return false;
            out.insert(out.end(), b.p + b.pos, b.p + b.pos + len);
            b.pos += len;
        } else if (type == 1 || type == 2) {
            Huff lit, dist;
            short lengths[320];
            if (type == 1) {
                int s;
                for (s = 0; s < 144; s++) lengths[s] = 8;
                for (; s < 256; s++) lengths[s] = 9;
                for (; s < 280; s++) lengths[s] = 7;
                for (; s < 288; s++) lengths[s] = 8;
                huff_build(lit, lengths, 288);
                for (s = 0; s < 30; s++) lengths[s] = 5;
                huff_build(dist, lengths, 30);
            } else {
                int nlen = b.get(5) + 257, ndist = b.get(5) + 1, ncode = b.get(4) + 4;
                if (b.err || nlen > 286 || ndist > 30) return false;
                int idx;
                for (idx = 0; idx < ncode; idx++) lengths[ORDER[idx]] = (short)b.get(3);
                for (; idx < 19; idx++) lengths[ORDER[idx]] = 0;
                Huff lc;
                if (huff_build(lc, lengths, 19) != 0) return false;
                idx = 0;
                while (idx < nlen + ndist) {
                    int sym = huff_decode(b, lc);
                    if (sym < 0 || b.err) return false;
                    if (sym < 16) {
                        lengths[idx++] = (short)sym;
                    } else {
                        int len = 0, rep;
                        if (sym == 16) {
                            if (idx == 0) return false;
                            len = lengths[idx - 1];
                            rep = 3 + b.get(2);
                        } else if (sym == 17) {
                            rep = 3 + b.get(3);
                        } else {
                            rep = 11 + b.get(7);
                        }
                        if (idx + rep > nlen + ndist) return false;
                        while (rep--) lengths[idx++] = (short)len;
                    }
                }
                if (lengths[256] == 0) return false;
                if (huff_build(lit, lengths, nlen) < 0) return false;
                if (huff_build(dist, lengths + nlen, ndist) < 0) return false;
            }
            for (;;) {
                int sym = huff_decode(b, lit);
                if (sym < 0 || b.err) return false;
                if (sym < 256) {
                    if (out.size() >= max_out) return false;
                    out.push_back((uint8_t)sym);
                } else if (sym == 256) {
                    break;
                } else {
                    sym -= 257;
                    if (sym >= 29) return false;
                    int len = LENS[sym] + b.get(LEXT[sym]);
                    int ds = huff_decode(b, dist);
                    if (ds < 0 || ds >= 30 || b.err) return false;
                    unsigned d = (unsigned)(DISTS[ds] + b.get(DEXT[ds]));
                    if (b.err || d > out.size()) return false;
                    if (out.size() + (size_t)len > max_out) return false;
                    size_t start = out.size() - d;
                    for (int i = 0; i < len; i++) {
                        uint8_t c = out[start + i];
                        out.push_back(c);
                    }
                }
            }
        } else {
            return false;
        }
    } while (!last);
    return true;
}

// ---------------------------------------------------------------- zip
struct ZipEntry {
    std::string name;
    uint16_t method = 0;
    uint32_t csize = 0, usize = 0, lho = 0;
};

inline std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

struct Zip {
    const uint8_t* d = nullptr;
    size_t n = 0;
    std::vector<ZipEntry> e;

    static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    static uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

    bool open(const uint8_t* data, size_t size) {
        d = data;
        n = size;
        e.clear();
        if (size < 22) return false;
        size_t minpos = size > 65557 ? size - 65557 : 0;
        size_t i = size - 22;
        bool found = false;
        for (;;) {
            if (u32(d + i) == 0x06054b50) { found = true; break; }
            if (i == minpos) break;
            i--;
        }
        if (!found) return false;
        uint16_t total = u16(d + i + 10);
        uint32_t cdsize = u32(d + i + 12), cdoff = u32(d + i + 16);
        if ((uint64_t)cdoff + cdsize > size) return false;
        size_t p = cdoff;
        for (int k = 0; k < total; k++) {
            if (p + 46 > size || u32(d + p) != 0x02014b50) return false;
            ZipEntry z;
            z.method = u16(d + p + 10);
            z.csize = u32(d + p + 20);
            z.usize = u32(d + p + 24);
            uint16_t nl = u16(d + p + 28), el = u16(d + p + 30), cl = u16(d + p + 32);
            z.lho = u32(d + p + 42);
            if (p + 46 + nl > size) return false;
            z.name.assign((const char*)d + p + 46, nl);
            e.push_back(z);
            p += 46 + nl + el + cl;
        }
        return true;
    }

    const ZipEntry* find(const std::string& name) const {
        for (auto& x : e)
            if (x.name == name) return &x;
        std::string ln = lower(name);
        for (auto& x : e)
            if (lower(x.name) == ln) return &x;
        return nullptr;
    }

    bool read(const ZipEntry& z, std::vector<uint8_t>& out, size_t max_size = (size_t)1 << 30) const {
        out.clear();
        if ((uint64_t)z.lho + 30 > n || u32(d + z.lho) != 0x04034b50) return false;
        uint16_t nl = u16(d + z.lho + 26), el = u16(d + z.lho + 28);
        size_t start = (size_t)z.lho + 30 + nl + el;
        if (start + z.csize > n) return false;
        if (z.usize > max_size) return false;
        if (z.method == 0) {
            out.assign(d + start, d + start + z.csize);
            return true;
        }
        if (z.method == 8) {
            out.reserve(z.usize);
            if (!inflate_raw(d + start, z.csize, out, z.usize)) return false;
            return out.size() == z.usize;
        }
        return false;
    }
};

// ---------------------------------------------------------------- PNG
struct Decoded {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

inline bool png_decode(const std::vector<uint8_t>& d, Decoded& out) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (d.size() < 33 || memcmp(d.data(), sig, 8) != 0) return false;
    size_t p = 8;
    std::vector<uint8_t> idat, pal, trns;
    int W = 0, H = 0, depth = 0, ctype = 0, interlace = 0;
    while (p + 12 <= d.size()) {
        uint32_t len = be32(&d[p]);
        if ((uint64_t)p + 12 + len > d.size()) return false;
        const uint8_t* t = &d[p + 4];
        const uint8_t* body = &d[p + 8];
        if (!memcmp(t, "IHDR", 4)) {
            if (len < 13) return false;
            W = (int)be32(body);
            H = (int)be32(body + 4);
            depth = body[8];
            ctype = body[9];
            interlace = body[12];
        } else if (!memcmp(t, "PLTE", 4)) {
            pal.assign(body, body + len);
        } else if (!memcmp(t, "tRNS", 4)) {
            trns.assign(body, body + len);
        } else if (!memcmp(t, "IDAT", 4)) {
            idat.insert(idat.end(), body, body + len);
        } else if (!memcmp(t, "IEND", 4)) {
            break;
        }
        p += 12 + len;
    }
    if (W <= 0 || H <= 0 || W > 16384 || H > 16384 || interlace != 0) return false;
    int ch;
    switch (ctype) {
        case 0: ch = 1; break;
        case 2: ch = 3; break;
        case 3: ch = 1; break;
        case 4: ch = 2; break;
        case 6: ch = 4; break;
        default: return false;
    }
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) return false;
    size_t bitsPerPixel = (size_t)ch * depth;
    size_t rowBytes = ((size_t)W * bitsPerPixel + 7) / 8;
    size_t bpp = std::max<size_t>(1, bitsPerPixel / 8);
    size_t expected = (rowBytes + 1) * (size_t)H;
    if (expected > ((size_t)400 << 20) || idat.size() < 6) return false;
    std::vector<uint8_t> raw;
    raw.reserve(expected);
    if (!inflate_raw(idat.data() + 2, idat.size() - 2, raw, expected)) return false;
    if (raw.size() < expected) return false;

    std::vector<uint8_t> img(rowBytes * (size_t)H);
    for (int y = 0; y < H; y++) {
        int filter = raw[(size_t)y * (rowBytes + 1)];
        const uint8_t* src = &raw[(size_t)y * (rowBytes + 1) + 1];
        uint8_t* dst = &img[(size_t)y * rowBytes];
        const uint8_t* prev = y ? &img[(size_t)(y - 1) * rowBytes] : nullptr;
        for (size_t i = 0; i < rowBytes; i++) {
            int a = i >= bpp ? dst[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
            int v;
            switch (filter) {
                case 0: v = src[i]; break;
                case 1: v = src[i] + a; break;
                case 2: v = src[i] + b; break;
                case 3: v = src[i] + ((a + b) >> 1); break;
                case 4: {
                    int pp = a + b - c, pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
                    int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    v = src[i] + pr;
                    break;
                }
                default: return false;
            }
            dst[i] = (uint8_t)v;
        }
    }

    out.w = W;
    out.h = H;
    out.rgba.assign((size_t)W * H * 4, 255);
    auto sample = [&](const uint8_t* row, size_t idx) -> int {
        if (depth == 8) return row[idx];
        if (depth == 16) return (row[idx * 2] << 8) | row[idx * 2 + 1];
        size_t bit = idx * (size_t)depth;
        return (row[bit / 8] >> (8 - depth - (int)(bit % 8))) & ((1 << depth) - 1);
    };
    auto to8 = [&](int v) -> uint8_t {
        if (depth == 8) return (uint8_t)v;
        if (depth == 16) return (uint8_t)(v >> 8);
        return (uint8_t)(v * 255 / ((1 << depth) - 1));
    };
    for (int y = 0; y < H; y++) {
        const uint8_t* row = &img[(size_t)y * rowBytes];
        for (int x = 0; x < W; x++) {
            uint8_t* o = &out.rgba[((size_t)y * W + x) * 4];
            switch (ctype) {
                case 0: o[0] = o[1] = o[2] = to8(sample(row, x)); break;
                case 2:
                    o[0] = to8(sample(row, (size_t)x * 3));
                    o[1] = to8(sample(row, (size_t)x * 3 + 1));
                    o[2] = to8(sample(row, (size_t)x * 3 + 2));
                    break;
                case 3: {
                    size_t idx = (size_t)sample(row, x);
                    if (idx * 3 + 2 < pal.size()) {
                        o[0] = pal[idx * 3];
                        o[1] = pal[idx * 3 + 1];
                        o[2] = pal[idx * 3 + 2];
                    }
                    o[3] = idx < trns.size() ? trns[idx] : 255;
                    break;
                }
                case 4:
                    o[0] = o[1] = o[2] = to8(sample(row, (size_t)x * 2));
                    o[3] = to8(sample(row, (size_t)x * 2 + 1));
                    break;
                case 6:
                    o[0] = to8(sample(row, (size_t)x * 4));
                    o[1] = to8(sample(row, (size_t)x * 4 + 1));
                    o[2] = to8(sample(row, (size_t)x * 4 + 2));
                    o[3] = to8(sample(row, (size_t)x * 4 + 3));
                    break;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------- görüntü işleme
// Görüntünün içeriğini kapsayan (saydam/beyaz olmayan) dikdörtgene kırpar.
inline void crop_content(Decoded& im) {
    int minx = im.w, miny = im.h, maxx = -1, maxy = -1;
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++) {
            const uint8_t* p = &im.rgba[((size_t)y * im.w + x) * 4];
            if (p[3] > 16 && (p[0] < 245 || p[1] < 245 || p[2] < 245)) {
                minx = std::min(minx, x);
                maxx = std::max(maxx, x);
                miny = std::min(miny, y);
                maxy = std::max(maxy, y);
            }
        }
    if (maxx < 0) return;
    int mx = std::max(2, (maxx - minx) / 25), my = std::max(2, (maxy - miny) / 25);
    minx = std::max(0, minx - mx);
    miny = std::max(0, miny - my);
    maxx = std::min(im.w - 1, maxx + mx);
    maxy = std::min(im.h - 1, maxy + my);
    int nw = maxx - minx + 1, nh = maxy - miny + 1;
    std::vector<uint8_t> r((size_t)nw * nh * 4);
    for (int y = 0; y < nh; y++)
        memcpy(&r[(size_t)y * nw * 4], &im.rgba[((size_t)(miny + y) * im.w + minx) * 4], (size_t)nw * 4);
    im.rgba.swap(r);
    im.w = nw;
    im.h = nh;
}

// RGBA kaynağı beyaz zemin üzerine bindirip tw x th boyutuna alan-ortalamasıyla küçültür/büyütür (BGRA çıktı).
inline void resample_white(const std::vector<uint8_t>& rgba, int W, int H, int tw, int th, std::vector<uint8_t>& out) {
    out.assign((size_t)tw * th * 4, 255);
    for (int y = 0; y < th; y++) {
        double sy0 = (double)y * H / th, sy1 = (double)(y + 1) * H / th;
        int iy0 = std::min((int)sy0, H - 1), iy1 = std::min((int)std::ceil(sy1), H);
        if (iy1 <= iy0) iy1 = iy0 + 1;
        for (int x = 0; x < tw; x++) {
            double sx0 = (double)x * W / tw, sx1 = (double)(x + 1) * W / tw;
            int ix0 = std::min((int)sx0, W - 1), ix1 = std::min((int)std::ceil(sx1), W);
            if (ix1 <= ix0) ix1 = ix0 + 1;
            double r = 0, g = 0, b = 0, ws = 0;
            for (int iy = iy0; iy < iy1; iy++) {
                double wy = std::min(sy1, (double)iy + 1) - std::max(sy0, (double)iy);
                if (wy <= 0) wy = 1e-6;
                for (int ix = ix0; ix < ix1; ix++) {
                    double wx = std::min(sx1, (double)ix + 1) - std::max(sx0, (double)ix);
                    if (wx <= 0) wx = 1e-6;
                    double w = wx * wy;
                    const uint8_t* s = &rgba[((size_t)iy * W + ix) * 4];
                    double a = s[3] / 255.0;
                    r += w * (s[0] * a + 255.0 * (1 - a));
                    g += w * (s[1] * a + 255.0 * (1 - a));
                    b += w * (s[2] * a + 255.0 * (1 - a));
                    ws += w;
                }
            }
            uint8_t* o = &out[((size_t)y * tw + x) * 4];
            o[0] = (uint8_t)std::lround(b / ws);
            o[1] = (uint8_t)std::lround(g / ws);
            o[2] = (uint8_t)std::lround(r / ws);
            o[3] = 255;
        }
    }
}

// Bir veya birden çok görüntüyü cx x cx beyaz zeminli tek kareye dizer (kontak sayfası).
inline Image make_sheet(std::vector<Decoded>& ims, int cx) {
    Image out;
    out.w = out.h = cx;
    out.bgra.assign((size_t)cx * cx * 4, 255);
    int n = (int)ims.size();
    if (n == 0) return out;
    int cols = (int)std::ceil(std::sqrt((double)n));
    int rows = (n + cols - 1) / cols;
    int cell = cx / cols;
    int pad = std::max(1, cell / 25);
    int inner = std::max(1, cell - 2 * pad);
    int yoff = (cx - cell * rows) / 2;
    for (int i = 0; i < n; i++) {
        crop_content(ims[i]);
        const Decoded& im = ims[i];
        double sc = std::min((double)inner / im.w, (double)inner / im.h);
        int tw = std::max(1, (int)std::lround(im.w * sc)), th = std::max(1, (int)std::lround(im.h * sc));
        std::vector<uint8_t> tile;
        resample_white(im.rgba, im.w, im.h, tw, th, tile);
        int x0 = (i % cols) * cell + (cell - tw) / 2;
        int y0 = yoff + (i / cols) * cell + (cell - th) / 2;
        for (int y = 0; y < th; y++) {
            int yy = y0 + y;
            if (yy < 0 || yy >= cx) continue;
            for (int x = 0; x < tw; x++) {
                int xx = x0 + x;
                if (xx < 0 || xx >= cx) continue;
                memcpy(&out.bgra[((size_t)yy * cx + xx) * 4], &tile[((size_t)y * tw + x) * 4], 4);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- STL
inline bool parse_stl(const uint8_t* d, size_t n, std::vector<float>& tris) {
    tris.clear();
    if (n >= 84) {
        uint32_t cnt = Zip::u32(d + 80);
        uint64_t need = 84 + (uint64_t)cnt * 50;
        bool exact = need == n;
        bool loose = cnt > 0 && need <= n && memcmp(d, "solid", 5) != 0;
        if (exact || loose) {
            tris.resize((size_t)cnt * 9);
            for (uint32_t i = 0; i < cnt; i++) {
                const uint8_t* t = d + 84 + (size_t)i * 50 + 12;
                memcpy(&tris[(size_t)i * 9], t, 36);
            }
            return cnt > 0;
        }
    }
    // ASCII STL
    std::string s((const char*)d, n);
    size_t pos = 0;
    while ((pos = s.find("vertex", pos)) != std::string::npos) {
        pos += 6;
        for (int k = 0; k < 3; k++) {
            char* end = nullptr;
            double v = strtod(s.c_str() + pos, &end);
            pos = (size_t)(end - s.c_str());
            tris.push_back((float)v);
        }
    }
    tris.resize(tris.size() / 9 * 9);
    return !tris.empty();
}

// ---------------------------------------------------------------- 3MF geometri (yedek yol)
namespace detail {

inline const char* find_attr(const char* a, const char* b, const char* key) {
    size_t kl = strlen(key);
    for (const char* p = a; p + kl + 3 <= b; p++) {
        if (p[0] == ' ' && !strncmp(p + 1, key, kl) && p[1 + kl] == '=' && (p[2 + kl] == '"' || p[2 + kl] == '\'')) return p + 3 + kl;
    }
    return nullptr;
}

inline bool attr_str(const char* a, const char* b, const char* key, std::string& v) {
    const char* p = find_attr(a, b, key);
    if (!p) return false;
    char q = *(p - 1);
    const char* e = (const char*)memchr(p, q, (size_t)(b - p));
    if (!e) return false;
    v.assign(p, e);
    return true;
}

inline bool attr_num(const char* a, const char* b, const char* key, double& v) {
    const char* p = find_attr(a, b, key);
    if (!p) return false;
    v = strtod(p, nullptr);
    return true;
}

struct Mat {
    float m[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
};

inline Mat parse_mat(const std::string& s) {
    Mat r;
    const char* p = s.c_str();
    for (int i = 0; i < 12; i++) {
        char* end = nullptr;
        double v = strtod(p, &end);
        if (end == p) return Mat();
        r.m[i] = (float)v;
        p = end;
    }
    return r;
}

// out = a * b (satır vektörü kuralı: v' = v * a * b)
inline Mat mul(const Mat& a, const Mat& b) {
    Mat r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
    for (int j = 0; j < 3; j++) r.m[9 + j] = a.m[9] * b.m[j] + a.m[10] * b.m[3 + j] + a.m[11] * b.m[6 + j] + b.m[9 + j];
    return r;
}

struct Comp {
    std::string path;
    int id = 0;
    Mat m;
};
struct Obj {
    std::vector<float> tris;
    std::vector<Comp> comps;
};
struct Item {
    std::string path;
    int id = 0;
    Mat m;
};
using ObjMap = std::map<std::pair<std::string, int>, Obj>;

inline std::string norm_path(std::string p) {
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    return p;
}

inline void parse_model(const std::string& path, const std::string& s, ObjMap& objs, std::vector<Item>* items) {
    const char* base = s.c_str();
    const char* endAll = base + s.size();
    size_t p = 0;
    while ((p = s.find("<object ", p)) != std::string::npos) {
        size_t tagEnd = s.find('>', p);
        size_t objEnd = s.find("</object>", p);
        if (tagEnd == std::string::npos || objEnd == std::string::npos) break;
        double idv = 0;
        attr_num(base + p, base + tagEnd, "id", idv);
        Obj& o = objs[{path, (int)idv}];
        size_t ms = s.find("<mesh", tagEnd);
        if (ms != std::string::npos && ms < objEnd) {
            size_t me = s.find("</mesh>", ms);
            if (me == std::string::npos || me > objEnd) me = objEnd;
            std::vector<float> verts;
            size_t q = ms;
            while ((q = s.find("<vertex ", q)) != std::string::npos && q < me) {
                size_t te = s.find('>', q);
                if (te == std::string::npos) break;
                double x = 0, y = 0, z = 0;
                attr_num(base + q, base + te, "x", x);
                attr_num(base + q, base + te, "y", y);
                attr_num(base + q, base + te, "z", z);
                verts.push_back((float)x);
                verts.push_back((float)y);
                verts.push_back((float)z);
                q = te;
            }
            q = ms;
            size_t nv = verts.size() / 3;
            while ((q = s.find("<triangle ", q)) != std::string::npos && q < me) {
                size_t te = s.find('>', q);
                if (te == std::string::npos) break;
                double a = -1, b = -1, c = -1;
                attr_num(base + q, base + te, "v1", a);
                attr_num(base + q, base + te, "v2", b);
                attr_num(base + q, base + te, "v3", c);
                if (a >= 0 && b >= 0 && c >= 0 && (size_t)a < nv && (size_t)b < nv && (size_t)c < nv) {
                    for (double idx : {a, b, c})
                        for (int k = 0; k < 3; k++) o.tris.push_back(verts[(size_t)idx * 3 + k]);
                }
                q = te;
            }
        }
        size_t q = tagEnd;
        while ((q = s.find("<component ", q)) != std::string::npos && q < objEnd) {
            size_t te = s.find('>', q);
            if (te == std::string::npos) break;
            Comp c;
            double oid = 0;
            attr_num(base + q, base + te, "objectid", oid);
            c.id = (int)oid;
            std::string v;
            if (attr_str(base + q, base + te, "p:path", v)) c.path = norm_path(v);
            if (attr_str(base + q, base + te, "transform", v)) c.m = parse_mat(v);
            o.comps.push_back(c);
            q = te;
        }
        p = objEnd + 9;
    }
    if (items) {
        size_t bs = s.find("<build");
        size_t q = bs;
        while (bs != std::string::npos && (q = s.find("<item ", q)) != std::string::npos) {
            size_t te = s.find('>', q);
            if (te == std::string::npos || base + te > endAll) break;
            Item it;
            double oid = 0;
            attr_num(base + q, base + te, "objectid", oid);
            it.id = (int)oid;
            std::string v;
            it.path = path;
            if (attr_str(base + q, base + te, "p:path", v)) it.path = norm_path(v);
            if (attr_str(base + q, base + te, "transform", v)) it.m = parse_mat(v);
            items->push_back(it);
            q = te;
        }
    }
}

inline void emit(const ObjMap& objs, const std::string& path, int id, const Mat& M, int depth, std::vector<float>& out) {
    if (depth > 8) return;
    auto it = objs.find({path, id});
    if (it == objs.end()) return;
    const Obj& o = it->second;
    for (size_t i = 0; i + 2 < o.tris.size(); i += 3) {
        float x = o.tris[i], y = o.tris[i + 1], z = o.tris[i + 2];
        out.push_back(x * M.m[0] + y * M.m[3] + z * M.m[6] + M.m[9]);
        out.push_back(x * M.m[1] + y * M.m[4] + z * M.m[7] + M.m[10]);
        out.push_back(x * M.m[2] + y * M.m[5] + z * M.m[8] + M.m[11]);
    }
    for (const Comp& c : o.comps) emit(objs, c.path.empty() ? path : c.path, c.id, mul(c.m, M), depth + 1, out);
}

}  // namespace detail

inline bool load_3mf_mesh(const Zip& z, std::vector<float>& tris) {
    using namespace detail;
    tris.clear();
    ObjMap objs;
    std::vector<Item> items;
    std::string mainPath = "3D/3dmodel.model";
    const ZipEntry* me = z.find(mainPath);
    if (!me) {
        for (auto& x : z.e)
            if (lower(x.name).size() > 6 && lower(x.name).compare(lower(x.name).size() - 6, 6, ".model") == 0) { me = &x; mainPath = x.name; break; }
    }
    if (!me) return false;
    std::vector<uint8_t> buf;
    if (!z.read(*me, buf, (size_t)256 << 20)) return false;
    parse_model(mainPath, std::string(buf.begin(), buf.end()), objs, &items);
    for (auto& x : z.e) {
        std::string ln = lower(x.name);
        if (x.name == mainPath || ln.size() < 6 || ln.compare(ln.size() - 6, 6, ".model") != 0) continue;
        if (!z.read(x, buf, (size_t)256 << 20)) continue;
        parse_model(x.name, std::string(buf.begin(), buf.end()), objs, nullptr);
    }
    if (!items.empty()) {
        for (const Item& it : items) emit(objs, it.path, it.id, it.m, 0, tris);
    } else {
        Mat I;
        for (auto& kv : objs) emit(objs, kv.first.first, kv.first.second, I, 0, tris);
    }
    return !tris.empty();
}

// ---------------------------------------------------------------- 3B çizim (yazılım rasterizer)
inline Image render_mesh(const std::vector<float>& tris, int cx) {
    Image out;
    out.w = out.h = cx;
    out.bgra.assign((size_t)cx * cx * 4, 255);
    size_t nt = tris.size() / 9;
    if (!nt) return out;

    const double PI = 3.14159265358979323846;
    const double az = -60.0 * PI / 180.0, el = 25.0 * PI / 180.0;
    double dv[3] = {std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el)};  // kameraya doğru
    double fv[3] = {-dv[0], -dv[1], -dv[2]};
    double rv[3] = {fv[1], -fv[0], 0};
    double rl = std::sqrt(rv[0] * rv[0] + rv[1] * rv[1]);
    rv[0] /= rl;
    rv[1] /= rl;
    double uv[3] = {rv[1] * fv[2] - rv[2] * fv[1], rv[2] * fv[0] - rv[0] * fv[2], rv[0] * fv[1] - rv[1] * fv[0]};

    // Ekran uzayına izdüşüm
    std::vector<float> sx(nt * 3), sy(nt * 3), sz(nt * 3);
    double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
    for (size_t i = 0; i < nt * 3; i++) {
        const float* v = &tris[i * 3];
        if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) { sx[i] = sy[i] = sz[i] = 0; continue; }
        double x = v[0] * rv[0] + v[1] * rv[1] + v[2] * rv[2];
        double y = v[0] * uv[0] + v[1] * uv[1] + v[2] * uv[2];
        double z = v[0] * dv[0] + v[1] * dv[1] + v[2] * dv[2];
        sx[i] = (float)x;
        sy[i] = (float)y;
        sz[i] = (float)z;
        minx = std::min(minx, x); maxx = std::max(maxx, x);
        miny = std::min(miny, y); maxy = std::max(maxy, y);
    }
    if (maxx < minx) return out;
    const int SS = 2;
    const int S = cx * SS;
    double span = std::max(maxx - minx, maxy - miny);
    if (span <= 0) span = 1;
    double scale = 0.88 * S / span;
    double cxm = (minx + maxx) / 2, cym = (miny + maxy) / 2;

    std::vector<float> zbuf((size_t)S * S, -1e30f);
    std::vector<uint8_t> col((size_t)S * S * 3, 255);
    double L[3] = {0.4, -0.6, 0.7};
    double ll = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    for (auto& c : L) c /= ll;

    for (size_t t = 0; t < nt; t++) {
        const float* v = &tris[t * 9];
        double e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]}, e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
        double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        double nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!(nl > 0) || !std::isfinite(nl)) continue;
        double shade = 0.3 + 0.7 * std::min(1.0, std::fabs((n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / nl));
        uint8_t cr = (uint8_t)(255 * shade * 0.25), cg = (uint8_t)(255 * shade * 0.55), cb = (uint8_t)(255 * shade * 0.95);

        double X[3], Y[3], Z[3];
        for (int k = 0; k < 3; k++) {
            X[k] = (sx[t * 3 + k] - cxm) * scale + S / 2.0;
            Y[k] = S / 2.0 - (sy[t * 3 + k] - cym) * scale;
            Z[k] = sz[t * 3 + k];
        }
        double area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
        if (std::fabs(area) < 1e-9) continue;
        int x0 = std::max(0, (int)std::floor(std::min({X[0], X[1], X[2]})));
        int x1 = std::min(S - 1, (int)std::ceil(std::max({X[0], X[1], X[2]})));
        int y0 = std::max(0, (int)std::floor(std::min({Y[0], Y[1], Y[2]})));
        int y1 = std::min(S - 1, (int)std::ceil(std::max({Y[0], Y[1], Y[2]})));
        double inv = 1.0 / area;
        for (int y = y0; y <= y1; y++) {
            double py = y + 0.5;
            for (int x = x0; x <= x1; x++) {
                double px = x + 0.5;
                double w0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) * inv;
                double w1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) * inv;
                double w2 = 1.0 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float z = (float)(w0 * Z[0] + w1 * Z[1] + w2 * Z[2]);
                size_t idx = (size_t)y * S + x;
                if (z > zbuf[idx]) {
                    zbuf[idx] = z;
                    col[idx * 3] = cr;
                    col[idx * 3 + 1] = cg;
                    col[idx * 3 + 2] = cb;
                }
            }
        }
    }
    // SSxSS ortalama
    for (int y = 0; y < cx; y++)
        for (int x = 0; x < cx; x++) {
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < SS; dy++)
                for (int dx = 0; dx < SS; dx++) {
                    size_t i = ((size_t)(y * SS + dy) * S + (x * SS + dx)) * 3;
                    r += col[i]; g += col[i + 1]; b += col[i + 2];
                }
            uint8_t* o = &out.bgra[((size_t)y * cx + x) * 4];
            o[0] = (uint8_t)(b / (SS * SS));
            o[1] = (uint8_t)(g / (SS * SS));
            o[2] = (uint8_t)(r / (SS * SS));
            o[3] = 255;
        }
    return out;
}

// ---------------------------------------------------------------- üst düzey: dosya -> küçük resim
enum class Source { None, Plates, Embedded, Geometry };

inline bool thumb_from_3mf(const Zip& z, int cx, Image& out, Source* src = nullptr, bool allowEmbedded = true) {
    using namespace detail;
    std::vector<uint8_t> buf;
    if (allowEmbedded) {
        // 1) Slicer plaka önizlemeleri: Metadata/plate_<N>.png (hepsi tek kareye)
        std::vector<std::pair<int, const ZipEntry*>> plates;
        for (auto& x : z.e) {
            const std::string pre = "Metadata/plate_", suf = ".png";
            if (x.name.size() > pre.size() + suf.size() && x.name.compare(0, pre.size(), pre) == 0 &&
                x.name.compare(x.name.size() - suf.size(), suf.size(), suf) == 0) {
                std::string mid = x.name.substr(pre.size(), x.name.size() - pre.size() - suf.size());
                if (!mid.empty() && mid.find_first_not_of("0123456789") == std::string::npos && mid.size() < 6)
                    plates.push_back({atoi(mid.c_str()), &x});
            }
        }
        std::sort(plates.begin(), plates.end(), [](auto& a, auto& b) { return a.first < b.first; });
        std::vector<Decoded> decoded;
        for (auto& pl : plates) {
            Decoded d;
            if (z.read(*pl.second, buf, (size_t)64 << 20) && png_decode(buf, d)) decoded.push_back(std::move(d));
        }
        if (!decoded.empty()) {
            out = make_sheet(decoded, cx);
            if (src) *src = Source::Plates;
            return true;
        }
        // 2) OPC standart kapak resmi (_rels/.rels içindeki metadata/thumbnail)
        std::vector<std::string> cand;
        if (const ZipEntry* re = z.find("_rels/.rels")) {
            if (z.read(*re, buf, (size_t)4 << 20)) {
                std::string s(buf.begin(), buf.end());
                size_t q = 0;
                while ((q = s.find("<Relationship ", q)) != std::string::npos) {
                    size_t te = s.find('>', q);
                    if (te == std::string::npos) break;
                    std::string type, target;
                    attr_str(s.c_str() + q, s.c_str() + te, "Type", type);
                    attr_str(s.c_str() + q, s.c_str() + te, "Target", target);
                    if (lower(type).find("/metadata/thumbnail") != std::string::npos && !target.empty()) cand.push_back(norm_path(target));
                    q = te;
                }
            }
        }
        // 3) Adında thumbnail geçen herhangi bir PNG
        for (auto& x : z.e) {
            std::string ln = lower(x.name);
            if (ln.find("thumbnail") != std::string::npos && ln.size() > 4 && ln.compare(ln.size() - 4, 4, ".png") == 0) cand.push_back(x.name);
        }
        for (auto& name : cand) {
            const ZipEntry* ent = z.find(name);
            Decoded d;
            if (ent && z.read(*ent, buf, (size_t)64 << 20) && png_decode(buf, d)) {
                std::vector<Decoded> one;
                one.push_back(std::move(d));
                out = make_sheet(one, cx);
                if (src) *src = Source::Embedded;
                return true;
            }
        }
    }
    // 4) Yedek: geometriyi oku ve çiz
    std::vector<float> tris;
    if (load_3mf_mesh(z, tris)) {
        out = render_mesh(tris, cx);
        if (src) *src = Source::Geometry;
        return true;
    }
    return false;
}

inline bool make_thumbnail(const uint8_t* d, size_t n, int cx, Image& out, Source* src = nullptr, bool allowEmbedded = true) {
    if (cx < 16) cx = 16;
    if (cx > 1024) cx = 1024;
    if (src) *src = Source::None;
    if (n >= 4 && d[0] == 'P' && d[1] == 'K') {
        Zip z;
        if (!z.open(d, n)) return false;
        return thumb_from_3mf(z, cx, out, src, allowEmbedded);
    }
    std::vector<float> tris;
    if (parse_stl(d, n, tris)) {
        out = render_mesh(tris, cx);
        if (src) *src = Source::Geometry;
        return true;
    }
    return false;
}

}  // namespace mt
