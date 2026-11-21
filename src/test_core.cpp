// Komut satırı test aracı: model dosyasından PPM küçük resim üretir.
// Kullanım: test_core <girdi.stl|3mf> <cikti.ppm> [boyut] [--geometry]
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "core.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "kullanim: %s girdi cikti.ppm [boyut] [--geometry]\n", argv[0]);
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int cx = argc > 3 && argv[3][0] != '-' ? atoi(argv[3]) : 256;
    bool geom = false;
    for (int i = 3; i < argc; i++)
        if (!strcmp(argv[i], "--geometry")) geom = true;

    mt::Image img;
    mt::Source src;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = mt::make_thumbnail(data.data(), data.size(), cx, img, &src, !geom);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const char* names[] = {"yok", "slicer plakalari", "gomulu onizleme", "geometri"};
    printf("%s: %s, kaynak=%s, %dx%d, %.0f ms\n", argv[1], ok ? "OK" : "BASARISIZ", names[(int)src], img.w, img.h, ms);
    if (!ok) return 1;
    std::string outName = argv[2];
    FILE* o = fopen(argv[2], "wb");
    if (!o) {
        fprintf(stderr, "cikti dosyasi acilamadi: %s\n", argv[2]);
        return 3;
    }
    if (outName.size() > 4 && mt::lower(outName.substr(outName.size() - 4)) == ".bmp") {
        // 24-bit BMP (Windows'ta her programla açılır)
        uint32_t rowSize = ((uint32_t)img.w * 3 + 3) & ~3u, dataSize = rowSize * img.h, fileSize = 54 + dataSize;
        uint8_t hdr[54] = {'B', 'M'};
        auto put32 = [&](int off, uint32_t v) { for (int k = 0; k < 4; k++) hdr[off + k] = (uint8_t)(v >> (8 * k)); };
        put32(2, fileSize); put32(10, 54); put32(14, 40); put32(18, (uint32_t)img.w); put32(22, (uint32_t)img.h);
        hdr[26] = 1; hdr[28] = 24; put32(34, dataSize);
        fwrite(hdr, 1, 54, o);
        std::vector<uint8_t> row(rowSize, 0);
        for (int y = img.h - 1; y >= 0; y--) {
            for (int x = 0; x < img.w; x++) memcpy(&row[(size_t)x * 3], &img.bgra[((size_t)y * img.w + x) * 4], 3);
            fwrite(row.data(), 1, rowSize, o);
        }
    } else {
        fprintf(o, "P6\n%d %d\n255\n", img.w, img.h);
        for (size_t i = 0; i < (size_t)img.w * img.h; i++) {
            uint8_t rgb[3] = {img.bgra[i * 4 + 2], img.bgra[i * 4 + 1], img.bgra[i * 4]};
            fwrite(rgb, 1, 3, o);
        }
    }
    fclose(o);
    return 0;
}
