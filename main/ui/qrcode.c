#include "qrcode.h"

#include <string.h>

// Compact QR generator: byte mode, ECC level M, auto version 1-10.
// Algorithm after Nayuki's public-domain QR-Code-generator.

// --- Galois field GF(256) arithmetic for Reed-Solomon ------------------------
static uint8_t gf_exp[512];
static uint8_t gf_log[256];
static bool gf_ready;

static void gf_init(void)
{
    if (gf_ready) {
        return;
    }
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp[i] = (uint8_t)x;
        gf_log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) {
            x ^= 0x11D; // QR's primitive polynomial
        }
    }
    for (int i = 255; i < 512; i++) {
        gf_exp[i] = gf_exp[i - 255];
    }
    gf_ready = true;
}

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) {
        return 0;
    }
    return gf_exp[gf_log[a] + gf_log[b]];
}

// --- ECC parameters (level M) for versions 1-10 ------------------------------
// data codewords and ecc-per-block; versions with >1 block are handled by
// splitting evenly (all versions 1-10 at level M use a single block group with
// equal-size blocks, which keeps this table simple).
typedef struct {
    uint16_t total_codewords; // data + ecc
    uint8_t ecc_per_block;
    uint8_t num_blocks;
} qr_ecc_t;

// {total data+ecc codewords, ecc codewords per block, block count} level M.
static const qr_ecc_t QR_ECC_M[QR_MAX_VERSION + 1] = {
    {0, 0, 0},        // version 0 unused
    {26, 10, 1},      // v1
    {44, 16, 1},      // v2
    {70, 26, 1},      // v3
    {100, 18, 2},     // v4
    {134, 24, 2},     // v5
    {172, 16, 4},     // v6
    {196, 18, 4},     // v7
    {242, 22, 4},     // v8
    {292, 22, 5},     // v9
    {346, 26, 5},     // v10
};

static int qr_version_size(int ver) { return 17 + 4 * ver; }

static int qr_data_codewords(int ver)
{
    const qr_ecc_t *e = &QR_ECC_M[ver];
    return e->total_codewords - e->ecc_per_block * e->num_blocks;
}

// --- Reed-Solomon ECC for one block -----------------------------------------
static void rs_ecc(const uint8_t *data, int data_len, uint8_t *ecc, int ecc_len)
{
    uint8_t gen[64];
    memset(gen, 0, sizeof(gen));
    gen[0] = 1;
    int gen_len = 1;
    for (int i = 0; i < ecc_len; i++) {
        // multiply generator by (x - a^i)
        for (int j = gen_len; j > 0; j--) {
            gen[j] = gen[j - 1] ^ gf_mul(gen[j], gf_exp[i]);
        }
        gen[0] = gf_mul(gen[0], gf_exp[i]);
        gen_len++;
    }
    memset(ecc, 0, ecc_len);
    for (int i = 0; i < data_len; i++) {
        uint8_t factor = data[i] ^ ecc[0];
        memmove(ecc, ecc + 1, ecc_len - 1);
        ecc[ecc_len - 1] = 0;
        for (int j = 0; j < ecc_len; j++) {
            ecc[j] ^= gf_mul(gen[ecc_len - 1 - j], factor);
        }
    }
}

// --- Matrix helpers ----------------------------------------------------------
// Function-pattern mask, tracked so data placement/masking skip fixed modules.
static uint8_t s_func[QR_MAX_SIZE * QR_MAX_SIZE];

static void set_module(qr_code_t *q, int x, int y, int dark, int is_func)
{
    q->modules[y * q->size + x] = (uint8_t)(dark ? 1 : 0);
    if (is_func) {
        s_func[y * q->size + x] = 1;
    }
}

static void draw_finder(qr_code_t *q, int cx, int cy)
{
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= q->size || y < 0 || y >= q->size) {
                continue;
            }
            int ax = dx < 0 ? -dx : dx;
            int ay = dy < 0 ? -dy : dy;
            int d = ax > ay ? ax : ay;
            int dark = (d != 2 && d <= 3) ? 1 : 0; // 7x7 finder with 1-module ring
            set_module(q, x, y, dark, 1);
        }
    }
}

static const uint8_t QR_ALIGN_POS[QR_MAX_VERSION + 1][2] = {
    {0, 0}, {0, 0}, {6, 18}, {6, 22}, {6, 26}, {6, 30},
    {6, 34}, {6, 22}, {6, 24}, {6, 26}, {6, 28},
};
// note: versions 7-10 also have a 3rd coordinate; handled below.
static const uint8_t QR_ALIGN_3RD[QR_MAX_VERSION + 1] = {
    0, 0, 0, 0, 0, 0, 0, 38, 42, 46, 50,
};

static void draw_align(qr_code_t *q, int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int ax = dx < 0 ? -dx : dx;
            int ay = dy < 0 ? -dy : dy;
            int d = ax > ay ? ax : ay;
            int dark = (d != 1) ? 1 : 0;
            set_module(q, cx + dx, cy + dy, dark, 1);
        }
    }
}

static void draw_function_patterns(qr_code_t *q, int ver)
{
    int n = q->size;
    // finders + separators
    draw_finder(q, 3, 3);
    draw_finder(q, n - 4, 3);
    draw_finder(q, 3, n - 4);
    // timing patterns
    for (int i = 8; i < n - 8; i++) {
        int dark = (i % 2 == 0) ? 1 : 0;
        set_module(q, i, 6, dark, 1);
        set_module(q, 6, i, dark, 1);
    }
    // dark module
    set_module(q, 8, n - 8, 1, 1);
    // alignment patterns (skip where they overlap finders)
    if (ver >= 2) {
        int coords[3];
        int cnt = 0;
        coords[cnt++] = QR_ALIGN_POS[ver][0];
        coords[cnt++] = QR_ALIGN_POS[ver][1];
        if (QR_ALIGN_3RD[ver]) {
            coords[cnt++] = QR_ALIGN_3RD[ver];
        }
        for (int i = 0; i < cnt; i++) {
            for (int j = 0; j < cnt; j++) {
                int x = coords[i], y = coords[j];
                // skip if overlapping a finder corner
                if ((x <= 8 && y <= 8) || (x >= n - 9 && y <= 8) ||
                    (x <= 8 && y >= n - 9)) {
                    continue;
                }
                draw_align(q, x, y);
            }
        }
    }
    // reserve format-info areas as function modules (filled later)
    for (int i = 0; i < 9; i++) {
        if (i != 6) {
            s_func[8 * n + i] = 1;
            s_func[i * n + 8] = 1;
        }
    }
    for (int i = 0; i < 8; i++) {
        s_func[8 * n + (n - 1 - i)] = 1;
        s_func[(n - 1 - i) * n + 8] = 1;
    }
}

// Format info for ECC level M and a given mask (precomputed 15-bit strings).
static const uint16_t QR_FORMAT_M[8] = {
    0x5412, 0x5125, 0x5E7C, 0x5B4B, 0x45F9, 0x40CE, 0x4F97, 0x4AA0,
};

static void draw_format(qr_code_t *q, int mask)
{
    int n = q->size;
    uint16_t bits = QR_FORMAT_M[mask];
    for (int i = 0; i < 15; i++) {
        int dark = (bits >> i) & 1;
        // around top-left
        if (i < 6) {
            q->modules[i * n + 8] = (uint8_t)dark;
        } else if (i < 8) {
            q->modules[(i + 1) * n + 8] = (uint8_t)dark;
        } else if (i == 8) {
            q->modules[8 * n + 7] = (uint8_t)dark;
        } else {
            q->modules[8 * n + (14 - i)] = (uint8_t)dark;
        }
        // duplicate copy
        if (i < 8) {
            q->modules[8 * n + (n - 1 - i)] = (uint8_t)dark;
        } else {
            q->modules[(n - 15 + i) * n + 8] = (uint8_t)dark;
        }
    }
}

static int mask_bit(int mask, int x, int y)
{
    switch (mask) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (y / 2 + x / 3) % 2 == 0;
        case 5: return (x * y) % 2 + (x * y) % 3 == 0;
        case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
        default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    }
}

static void place_data(qr_code_t *q, const uint8_t *codewords, int total_bits)
{
    int n = q->size;
    int bit = 0;
    int dir = -1; // moving up
    for (int col = n - 1; col > 0; col -= 2) {
        if (col == 6) {
            col--; // skip timing column
        }
        for (int k = 0; k < n; k++) {
            int y = (dir < 0) ? (n - 1 - k) : k;
            for (int c = 0; c < 2; c++) {
                int x = col - c;
                if (s_func[y * n + x]) {
                    continue;
                }
                int dark = 0;
                if (bit < total_bits) {
                    dark = (codewords[bit >> 3] >> (7 - (bit & 7))) & 1;
                    bit++;
                }
                q->modules[y * n + x] = (uint8_t)dark;
            }
        }
        dir = -dir;
    }
}

static void apply_mask(qr_code_t *q, int mask)
{
    int n = q->size;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (s_func[y * n + x]) {
                continue;
            }
            if (mask_bit(mask, x, y)) {
                q->modules[y * n + x] ^= 1;
            }
        }
    }
}

// Penalty score for mask selection (rule 1 + rule 3 subset is enough here; we
// use the full four rules lightly to pick a reasonable mask).
static int penalty(const qr_code_t *q)
{
    int n = q->size;
    int score = 0;
    // rule 1: runs of 5+ same-color in rows/cols
    for (int y = 0; y < n; y++) {
        int run = 1;
        for (int x = 1; x < n; x++) {
            if (q->modules[y * n + x] == q->modules[y * n + x - 1]) {
                run++;
                if (run == 5) score += 3;
                else if (run > 5) score += 1;
            } else {
                run = 1;
            }
        }
    }
    for (int x = 0; x < n; x++) {
        int run = 1;
        for (int y = 1; y < n; y++) {
            if (q->modules[y * n + x] == q->modules[(y - 1) * n + x]) {
                run++;
                if (run == 5) score += 3;
                else if (run > 5) score += 1;
            } else {
                run = 1;
            }
        }
    }
    // rule 2: 2x2 blocks
    for (int y = 0; y < n - 1; y++) {
        for (int x = 0; x < n - 1; x++) {
            uint8_t v = q->modules[y * n + x];
            if (v == q->modules[y * n + x + 1] &&
                v == q->modules[(y + 1) * n + x] &&
                v == q->modules[(y + 1) * n + x + 1]) {
                score += 3;
            }
        }
    }
    return score;
}

bool qr_encode(qr_code_t *out, const char *text)
{
    gf_init();
    int len = (int)strlen(text);

    // pick smallest version that fits: byte mode header is 4 bits mode + 8/16
    // bit length. versions 1-9 use 8-bit count, 10 uses 16-bit.
    int ver = 0;
    for (int v = 1; v <= QR_MAX_VERSION; v++) {
        int count_bits = (v < 10) ? 8 : 16;
        int data_bits = 4 + count_bits + len * 8;
        int cap_bits = qr_data_codewords(v) * 8;
        if (data_bits + 4 <= cap_bits) { // +4 slack for terminator
            ver = v;
            break;
        }
    }
    if (ver == 0) {
        out->size = 0;
        return false; // too long for version 10
    }

    int data_cw = qr_data_codewords(ver);
    int count_bits = (ver < 10) ? 8 : 16;

    // --- build bit stream into a codeword buffer ---
    uint8_t buf[QR_ECC_M[QR_MAX_VERSION].total_codewords];
    memset(buf, 0, sizeof(buf));
    int bitpos = 0;
    #define PUT_BIT(b) do { if (b) buf[bitpos >> 3] |= 0x80 >> (bitpos & 7); bitpos++; } while (0)
    #define PUT_BITS(val, cnt) do { for (int _i = (cnt) - 1; _i >= 0; _i--) PUT_BIT(((val) >> _i) & 1); } while (0)

    PUT_BITS(0x4, 4);               // byte mode
    PUT_BITS(len, count_bits);      // char count
    for (int i = 0; i < len; i++) {
        PUT_BITS((uint8_t)text[i], 8);
    }
    // terminator (up to 4 bits) then pad to byte boundary
    int cap_bits = data_cw * 8;
    for (int i = 0; i < 4 && bitpos < cap_bits; i++) {
        PUT_BIT(0);
    }
    while (bitpos % 8 != 0) {
        PUT_BIT(0);
    }
    // pad bytes 0xEC 0x11 alternating
    int pad = 0;
    while ((bitpos >> 3) < data_cw) {
        PUT_BITS(pad ? 0x11 : 0xEC, 8);
        pad ^= 1;
    }
    #undef PUT_BIT
    #undef PUT_BITS

    // --- split into blocks, compute ECC, interleave ---
    const qr_ecc_t *ep = &QR_ECC_M[ver];
    int nb = ep->num_blocks;
    int ecc_len = ep->ecc_per_block;
    int short_len = data_cw / nb;              // data cw per block
    int long_count = data_cw % nb;             // blocks with one extra cw
    // In versions 1-10 level M all blocks are equal (long_count==0), but keep
    // general handling for safety.
    uint8_t final_cw[QR_ECC_M[QR_MAX_VERSION].total_codewords];
    int fpos = 0;
    uint8_t ecc_blocks[16][32];
    int block_data_len[16];
    int offsets[16];
    int off = 0;
    for (int b = 0; b < nb; b++) {
        int dlen = short_len + (b >= nb - long_count ? 1 : 0);
        block_data_len[b] = dlen;
        offsets[b] = off;
        rs_ecc(buf + off, dlen, ecc_blocks[b], ecc_len);
        off += dlen;
    }
    // interleave data codewords
    int maxd = short_len + (long_count ? 1 : 0);
    for (int i = 0; i < maxd; i++) {
        for (int b = 0; b < nb; b++) {
            if (i < block_data_len[b]) {
                final_cw[fpos++] = buf[offsets[b] + i];
            }
        }
    }
    // interleave ecc codewords
    for (int i = 0; i < ecc_len; i++) {
        for (int b = 0; b < nb; b++) {
            final_cw[fpos++] = ecc_blocks[b][i];
        }
    }

    // --- render matrix ---
    out->size = qr_version_size(ver);
    memset(out->modules, 0, out->size * out->size);
    memset(s_func, 0, out->size * out->size);
    draw_function_patterns(out, ver);
    place_data(out, final_cw, fpos * 8);

    // choose mask with lowest penalty
    int best_mask = 0, best_score = 0x7FFFFFFF;
    qr_code_t trial;
    for (int m = 0; m < 8; m++) {
        memcpy(&trial, out, sizeof(qr_code_t));
        apply_mask(&trial, m);
        draw_format(&trial, m);
        int s = penalty(&trial);
        if (s < best_score) {
            best_score = s;
            best_mask = m;
        }
    }
    apply_mask(out, best_mask);
    draw_format(out, best_mask);
    return true;
}

// Escapes ; , : and backslash per the Wi-Fi QR format.
static void append_escaped(char *dst, size_t cap, size_t *pos, const char *src)
{
    for (; *src && *pos + 2 < cap; src++) {
        char c = *src;
        if (c == ';' || c == ',' || c == ':' || c == '\\' || c == '"') {
            dst[(*pos)++] = '\\';
        }
        dst[(*pos)++] = c;
    }
}

bool qr_encode_wifi(qr_code_t *out, const char *ssid, const char *password)
{
    char payload[160];
    size_t pos = 0;
    const char *pfx = "WIFI:T:WPA;S:";
    for (const char *p = pfx; *p && pos < sizeof(payload) - 1; p++) {
        payload[pos++] = *p;
    }
    append_escaped(payload, sizeof(payload), &pos, ssid);
    const char *mid = ";P:";
    for (const char *p = mid; *p && pos < sizeof(payload) - 1; p++) {
        payload[pos++] = *p;
    }
    append_escaped(payload, sizeof(payload), &pos, password);
    const char *end = ";;";
    for (const char *p = end; *p && pos < sizeof(payload) - 1; p++) {
        payload[pos++] = *p;
    }
    payload[pos] = '\0';
    return qr_encode(out, payload);
}
