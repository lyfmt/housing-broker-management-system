#include "password_utils.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PASSWORD_HASH_TAG "H$"
#define PASSWORD_HASH_SALT_HEX_LEN 8
#define PASSWORD_HASH_DIGEST_BYTES 10
#define PASSWORD_HASH_DIGEST_HEX_LEN (PASSWORD_HASH_DIGEST_BYTES * 2)
#define PASSWORD_HASH_FORMAT_LEN (2 + PASSWORD_HASH_SALT_HEX_LEN + 1 + PASSWORD_HASH_DIGEST_HEX_LEN)
#define PASSWORD_HASH_ITERATIONS 4096
#define TEMP_PASSWORD_LEN 10

typedef struct {
    uint32_t state[8];
    uint64_t totalLen;
    unsigned char buffer[64];
    size_t bufferLen;
} Sha256Ctx;

static unsigned int g_password_nonce = 0;

static void secure_zero(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    if (!p) return;
    while (len--) *p++ = 0;
}

static uint32_t rotr32(uint32_t value, unsigned int shift) {
    return (value >> shift) | (value << (32u - shift));
}

static void sha256_transform(Sha256Ctx *ctx, const unsigned char block[64]) {
    static const uint32_t k[64] = {
        0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
        0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
        0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
        0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
        0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
        0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
        0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
        0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
        0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
        0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
        0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
        0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
        0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
        0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
        0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
        0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
    };
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24)
             | ((uint32_t)block[i * 4 + 1] << 16)
             | ((uint32_t)block[i * 4 + 2] << 8)
             | (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(Sha256Ctx *ctx) {
    if (!ctx) return;
    ctx->state[0] = 0x6A09E667u;
    ctx->state[1] = 0xBB67AE85u;
    ctx->state[2] = 0x3C6EF372u;
    ctx->state[3] = 0xA54FF53Au;
    ctx->state[4] = 0x510E527Fu;
    ctx->state[5] = 0x9B05688Cu;
    ctx->state[6] = 0x1F83D9ABu;
    ctx->state[7] = 0x5BE0CD19u;
    ctx->totalLen = 0;
    ctx->bufferLen = 0;
}

static void sha256_update(Sha256Ctx *ctx, const void *data, size_t len) {
    const unsigned char *bytes = (const unsigned char *)data;
    if (!ctx || (!bytes && len != 0)) return;

    ctx->totalLen += len;
    while (len > 0) {
        size_t take = sizeof(ctx->buffer) - ctx->bufferLen;
        if (take > len) take = len;
        memcpy(ctx->buffer + ctx->bufferLen, bytes, take);
        ctx->bufferLen += take;
        bytes += take;
        len -= take;
        if (ctx->bufferLen == sizeof(ctx->buffer)) {
            sha256_transform(ctx, ctx->buffer);
            ctx->bufferLen = 0;
        }
    }
}

static void sha256_final(Sha256Ctx *ctx, unsigned char out[32]) {
    uint64_t bitLen;
    size_t i;

    if (!ctx || !out) return;

    ctx->buffer[ctx->bufferLen++] = 0x80u;
    if (ctx->bufferLen > 56) {
        while (ctx->bufferLen < 64) ctx->buffer[ctx->bufferLen++] = 0;
        sha256_transform(ctx, ctx->buffer);
        ctx->bufferLen = 0;
    }
    while (ctx->bufferLen < 56) ctx->buffer[ctx->bufferLen++] = 0;

    bitLen = ctx->totalLen * 8u;
    for (i = 0; i < 8; ++i) {
        ctx->buffer[56 + i] = (unsigned char)((bitLen >> ((7u - i) * 8u)) & 0xFFu);
    }
    sha256_transform(ctx, ctx->buffer);

    for (i = 0; i < 8; ++i) {
        out[i * 4] = (unsigned char)((ctx->state[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = (unsigned char)((ctx->state[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = (unsigned char)((ctx->state[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = (unsigned char)(ctx->state[i] & 0xFFu);
    }
}

static int hex_value(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static void bytes_to_hex(const unsigned char *src, size_t len, char *out) {
    static const char digits[] = "0123456789ABCDEF";
    size_t i;
    if (!src || !out) return;
    for (i = 0; i < len; ++i) {
        out[i * 2] = digits[(src[i] >> 4) & 0x0F];
        out[i * 2 + 1] = digits[src[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

static void u32_to_hex(uint32_t value, char out[PASSWORD_HASH_SALT_HEX_LEN + 1]) {
    int i;
    if (!out) return;
    for (i = PASSWORD_HASH_SALT_HEX_LEN - 1; i >= 0; --i) {
        out[i] = "0123456789ABCDEF"[value & 0x0Fu];
        value >>= 4;
    }
    out[PASSWORD_HASH_SALT_HEX_LEN] = '\0';
}

static int parse_hex_bytes(const char *src, unsigned char *out, size_t outLen) {
    size_t i;
    if (!src || !out) return 0;
    for (i = 0; i < outLen; ++i) {
        int hi = hex_value((unsigned char)src[i * 2]);
        int lo = hex_value((unsigned char)src[i * 2 + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 1;
}

static int parse_hex_u32(const char *src, uint32_t *value) {
    int i;
    uint32_t result = 0;
    if (!src || !value) return 0;
    for (i = 0; i < PASSWORD_HASH_SALT_HEX_LEN; ++i) {
        int nibble = hex_value((unsigned char)src[i]);
        if (nibble < 0) return 0;
        result = (result << 4) | (uint32_t)nibble;
    }
    *value = result;
    return 1;
}

static int secure_bytes_equal(const unsigned char *a, const unsigned char *b, size_t len) {
    size_t i;
    unsigned char diff = 0;
    if (!a || !b) return 0;
    for (i = 0; i < len; ++i) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

static uint32_t next_password_entropy(void) {
    uint32_t mix = (uint32_t)time(NULL);
    mix ^= (uint32_t)clock();
    mix ^= (uint32_t)rand();
    mix ^= (uint32_t)(++g_password_nonce * 2654435761u);
    mix ^= (uint32_t)(uintptr_t)&mix;
    if (mix == 0) mix = 0xA5C31F27u ^ (uint32_t)g_password_nonce;
    return mix;
}

static void derive_password_digest(uint32_t salt, const char *password, unsigned char out[PASSWORD_HASH_DIGEST_BYTES]) {
    unsigned char digest[32];
    unsigned char saltBytes[4];
    Sha256Ctx ctx;
    const char *pwd = password ? password : "";
    size_t pwdLen = strlen(pwd);
    int i;

    saltBytes[0] = (unsigned char)((salt >> 24) & 0xFFu);
    saltBytes[1] = (unsigned char)((salt >> 16) & 0xFFu);
    saltBytes[2] = (unsigned char)((salt >> 8) & 0xFFu);
    saltBytes[3] = (unsigned char)(salt & 0xFFu);

    sha256_init(&ctx);
    sha256_update(&ctx, saltBytes, sizeof(saltBytes));
    if (pwdLen) sha256_update(&ctx, pwd, pwdLen);
    sha256_final(&ctx, digest);

    for (i = 1; i < PASSWORD_HASH_ITERATIONS; ++i) {
        sha256_init(&ctx);
        sha256_update(&ctx, digest, sizeof(digest));
        sha256_update(&ctx, saltBytes, sizeof(saltBytes));
        if (pwdLen) sha256_update(&ctx, pwd, pwdLen);
        sha256_final(&ctx, digest);
    }

    memcpy(out, digest, PASSWORD_HASH_DIGEST_BYTES);
    secure_zero(digest, sizeof(digest));
    secure_zero(&ctx, sizeof(ctx));
}

static int password_parse_hash(const char *stored, uint32_t *salt, unsigned char digest[PASSWORD_HASH_DIGEST_BYTES]) {
    if (!stored) return 0;
    if (strncmp(stored, PASSWORD_HASH_TAG, strlen(PASSWORD_HASH_TAG)) != 0) return 0;
    if (strlen(stored) != PASSWORD_HASH_FORMAT_LEN) return 0;
    if (stored[2 + PASSWORD_HASH_SALT_HEX_LEN] != '$') return 0;
    if (!parse_hex_u32(stored + 2, salt)) return 0;
    if (!parse_hex_bytes(stored + 3 + PASSWORD_HASH_SALT_HEX_LEN, digest, PASSWORD_HASH_DIGEST_BYTES)) return 0;
    return 1;
}

static int password_is_hashed(const char *stored) {
    uint32_t salt = 0;
    unsigned char digest[PASSWORD_HASH_DIGEST_BYTES];
    int ok = password_parse_hash(stored, &salt, digest);
    secure_zero(digest, sizeof(digest));
    return ok;
}

void password_store(char *dest, size_t size, const char *password) {
    char encoded[32];
    char saltHex[PASSWORD_HASH_SALT_HEX_LEN + 1];
    char digestHex[PASSWORD_HASH_DIGEST_HEX_LEN + 1];
    unsigned char digest[PASSWORD_HASH_DIGEST_BYTES];
    uint32_t salt;

    if (!dest || size == 0) return;
    if (!password || !password[0]) {
        dest[0] = '\0';
        return;
    }
    if (password_is_hashed(password)) {
        strncpy(dest, password, size - 1);
        dest[size - 1] = '\0';
        return;
    }

    salt = next_password_entropy();
    derive_password_digest(salt, password, digest);
    u32_to_hex(salt, saltHex);
    bytes_to_hex(digest, sizeof(digest), digestHex);
    snprintf(encoded, sizeof(encoded), "%s%s$%s", PASSWORD_HASH_TAG, saltHex, digestHex);
    strncpy(dest, encoded, size - 1);
    dest[size - 1] = '\0';
    secure_zero(encoded, sizeof(encoded));
    secure_zero(digest, sizeof(digest));
}

int password_verify(const char *stored, const char *input) {
    uint32_t salt = 0;
    unsigned char expected[PASSWORD_HASH_DIGEST_BYTES];
    unsigned char actual[PASSWORD_HASH_DIGEST_BYTES];

    if (!stored || !input) return 0;
    if (!password_parse_hash(stored, &salt, expected)) {
        return strcmp(stored, input) == 0;
    }

    derive_password_digest(salt, input, actual);
    if (secure_bytes_equal(expected, actual, sizeof(actual))) {
        secure_zero(expected, sizeof(expected));
        secure_zero(actual, sizeof(actual));
        return 1;
    }
    secure_zero(expected, sizeof(expected));
    secure_zero(actual, sizeof(actual));
    return 0;
}

static int normalize_password_field(char *field, size_t size) {
    char encoded[32];
    if (!field || !field[0] || password_is_hashed(field)) return 0;
    password_store(encoded, sizeof(encoded), field);
    strncpy(field, encoded, size - 1);
    field[size - 1] = '\0';
    secure_zero(encoded, sizeof(encoded));
    return 1;
}

int normalize_database_passwords(Database *db) {
    int changed = 0;
    AgentNode *a;
    TenantNode *t;

    if (!db) return 0;
    changed |= normalize_password_field(db->adminPassword, sizeof(db->adminPassword));
    for (a = db->agents; a; a = a->next) {
        changed |= normalize_password_field(a->data.password, sizeof(a->data.password));
    }
    for (t = db->tenants; t; t = t->next) {
        changed |= normalize_password_field(t->data.password, sizeof(t->data.password));
    }
    return changed;
}

void generate_temporary_password(char *out, size_t size) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    size_t i;
    size_t alphabetLen = sizeof(alphabet) - 1;
    uint32_t entropy = 0;

    if (!out || size == 0) return;
    if (size <= TEMP_PASSWORD_LEN) {
        out[0] = '\0';
        return;
    }

    for (i = 0; i < TEMP_PASSWORD_LEN; ++i) {
        if ((i % 4u) == 0) entropy = next_password_entropy();
        out[i] = alphabet[entropy % alphabetLen];
        entropy = (entropy / (uint32_t)alphabetLen) ^ (entropy >> 11);
    }
    out[TEMP_PASSWORD_LEN] = '\0';
}
