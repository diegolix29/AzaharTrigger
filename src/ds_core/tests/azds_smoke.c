// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Headless smoke test for the azalon_ds library:
//   azds_smoke <rom.nds> [frames] [out.png]
// Boots the ROM, runs N frames (pressing A now and then), reports audio output
// and writes both screens stacked into a PNG.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "azalon_ds/azalon_ds.h"

static void on_log(void* ud, azds_log_level level, const char* msg) {
    (void)ud;
    if (level >= AZDS_LOG_INFO)
        printf("  [core] %s\n", msg);
}

// --- minimal PNG writer (stored/uncompressed deflate) ---------------------

static unsigned long crc_table[256];
static void crc_init(void) {
    for (unsigned long n = 0; n < 256; n++) {
        unsigned long c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static unsigned long crc_update(unsigned long crc, const unsigned char* buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}
static void put32(FILE* f, unsigned long v) {
    unsigned char b[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16),
                          (unsigned char)(v >> 8), (unsigned char)v};
    fwrite(b, 1, 4, f);
}
static void chunk(FILE* f, const char* type, const unsigned char* data, size_t len) {
    put32(f, (unsigned long)len);
    fwrite(type, 1, 4, f);
    if (len)
        fwrite(data, 1, len, f);
    unsigned long crc = crc_update(0xFFFFFFFFUL, (const unsigned char*)type, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFUL;
    put32(f, crc);
}

static int write_png(const char* path, const uint32_t* bgra, int w, int h) {
    const size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    unsigned char* raw = malloc(raw_len);
    for (int y = 0; y < h; y++) {
        unsigned char* row = raw + (size_t)y * (1 + (size_t)w * 3);
        row[0] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t p = bgra[y * w + x];
            row[1 + x * 3 + 0] = (unsigned char)(p >> 16);
            row[1 + x * 3 + 1] = (unsigned char)(p >> 8);
            row[1 + x * 3 + 2] = (unsigned char)p;
        }
    }
    // zlib stream with stored blocks
    const size_t nblocks = (raw_len + 65534) / 65535;
    const size_t z_len = 2 + raw_len + nblocks * 5 + 4;
    unsigned char* z = malloc(z_len);
    size_t o = 0;
    z[o++] = 0x78;
    z[o++] = 0x01;
    unsigned long a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw_len; pos += 65535) {
        size_t n = raw_len - pos < 65535 ? raw_len - pos : 65535;
        z[o++] = (pos + n >= raw_len) ? 1 : 0;
        z[o++] = (unsigned char)n;
        z[o++] = (unsigned char)(n >> 8);
        z[o++] = (unsigned char)~n;
        z[o++] = (unsigned char)(~n >> 8);
        memcpy(z + o, raw + pos, n);
        o += n;
    }
    unsigned long adler = (b << 16) | a;
    z[o++] = (unsigned char)(adler >> 24);
    z[o++] = (unsigned char)(adler >> 16);
    z[o++] = (unsigned char)(adler >> 8);
    z[o++] = (unsigned char)adler;

    FILE* f = fopen(path, "wb");
    if (!f) {
        free(raw);
        free(z);
        return 0;
    }
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13] = {(unsigned char)(w >> 24), (unsigned char)(w >> 16),
                              (unsigned char)(w >> 8),  (unsigned char)w,
                              (unsigned char)(h >> 24), (unsigned char)(h >> 16),
                              (unsigned char)(h >> 8),  (unsigned char)h,
                              8, 2, 0, 0, 0};
    crc_init();
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <rom.nds> [frames] [out.png]\n", argv[0]);
        return 2;
    }
    const char* rom = argv[1];
    const int frames = argc > 2 ? atoi(argv[2]) : 600;
    const char* png = argc > 3 ? argv[3] : "azds_smoke.png";

    printf("azalon_ds API %d, melonDS %s\n", azds_api_version(), azds_melonds_version());

    azds_rom_info info;
    if (azds_probe_rom(rom, &info))
        printf("ROM: code=%s header='%s' title='%s' icon=%d homebrew=%d dsi=%d/%d\n",
               info.game_code, info.header_title, info.title, info.has_icon, info.is_homebrew,
               info.is_dsi_enhanced, info.is_dsi_exclusive);

    azds_config cfg;
    azds_config_init(&cfg);
    cfg.data_dir = "azds_smoke_data";
    cfg.dldi_image_path = "azds_smoke_data/dldi_sd.bin";
    cfg.dldi_image_size = 64ull * 1024 * 1024;
    cfg.callbacks.log = on_log;

    char err[512] = {0};
    azds_console* ds = azds_create(&cfg, err, sizeof(err));
    if (!ds) {
        printf("create failed: %s\n", err);
        return 1;
    }
    if (!azds_load_rom(ds, rom, "azds_smoke_data/smoke.sav", err, sizeof(err))) {
        printf("load failed: %s\n", err);
        azds_destroy(ds);
        return 1;
    }
    if (!azds_start(ds)) {
        printf("start failed\n");
        azds_destroy(ds);
        return 1;
    }

    long audio_frames = 0;
    int16_t audio[2 * 2048];
    unsigned long lines = 0;
    for (int i = 0; i < frames; i++) {
        // tap A for a few frames every second to get past title screens
        azds_set_keys(ds, (i % 60) < 5 ? AZDS_KEY_A : 0);
        lines += azds_run_frame(ds);
        int n;
        while ((n = azds_read_audio(ds, audio, 2048)) > 0)
            audio_frames += n;
    }
    printf("ran %d frames (%lu scanlines), audio frames=%ld (~%.0f Hz)\n", frames, lines,
           audio_frames, audio_frames / (frames / 59.8261));

    const uint32_t *top = NULL, *bottom = NULL;
    if (!azds_get_framebuffers(ds, &top, &bottom)) {
        printf("no framebuffers\n");
        azds_destroy(ds);
        return 1;
    }
    static uint32_t image[256 * 384];
    memcpy(image, top, 256 * 192 * 4);
    memcpy(image + 256 * 192, bottom, 256 * 192 * 4);
    int distinct = 0;
    for (int i = 1; i < 256 * 384 && distinct < 2; i++)
        if ((image[i] & 0xFFFFFF) != (image[0] & 0xFFFFFF))
            distinct = 2;
    printf("screens %s\n", distinct ? "have content" : "are a single flat colour");
    printf("png: %s\n", write_png(png, image, 256, 384) ? png : "(write failed)");

    azds_destroy(ds);
    return 0;
}
