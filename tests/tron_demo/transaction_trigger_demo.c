#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pb_common.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include "core/Contract.pb.h"
#include "transaction_trigger_core.h"
#include "google/protobuf/any.pb.h"

static size_t min_size(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return (int)(c - '0');
    if (c >= 'a' && c <= 'f')
        return 10 + (int)(c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (int)(c - 'A');
    return -1;
}

/* Large ABI payload kept as hex string for the demo. */
/* ---- Demo data based on Nile transaction ----
 * ffd9281d2abebb7e9ad01e9cc42411de8713258d1fc2084842d9df3759a52524
 */
static const char k_call_data_hex[] =
    "855d175e"
    "0000000000000000000000000000000000000000000000003782dace9d900000"
    "432464fe1e9c33eaf99edad710ef11359d0291506bc81f7445f1447ba112ab18"
    "30579dd4decdd0ed38f33215375c00e1fb324f0f35892991aa0b8f5eb2a9dfcb"
    "1533ca563b77de5177a41e8ffefb2fd688334af95bcd1143470318c026b00e2e"
    "86227c50592880dd3f0c362258714e50b9b9d54eb193b0bba6cc04cf348d841d"
    "55261810658d9eeaeb2920005179f9e099c9eaf06c793ccc3ec2f30c8f52d939"
    "4d0f6084bc53f142663d713f6ef575db86b0c55abfe9aacf6515bb0a5986a0b2"
    "14bc1c48c9678906c88a4c625336b8d8a9be49d1ec75762bb60338f738ad9989"
    "1b98ce18c723e76e70f53dce7c03b247aa937354a6954feac4f8858ae5830b3c"
    "fc0466421864b2ffc5740dc19210e691d5d4fccf9a3e7d1f4ef6e2a676975dec"
    "cd63c5990d8275dc35d0922be3bd5dc7c4a81494e8276b282e61a19fbe9cca05"
    "32592b05623b7bffee09ef81fb298cbaf4ece9c42fe5ac80a716e90c7dff7f01"
    "2c1215f739c89e715ffab693eff699b5eb6368753af0c0087274db0f14c38ce7"
    "b0b36100f9a2f51cc7f6eca87eb750544f9fbe18ea79dbd8595028c4d33da6ee"
    "62afe4eca52cfe40f164f00063abe1c76e3d0fc0af2db17aad96b3f472d41f0b"
    "56d8dd4fa5b9d6de028678731425871cbf134701c351aba7edeaad07e8d40060"
    "1e245bc1751ebb514365c774e5108c348a39533e836a10338ab8ae00ceb0f819"
    "f3f45268ee88bdf9fdadf6c921269d707fe457fa25d6997a5da382ac8b4f960a"
    "381ab539c33421931569785dbe6dfa59914ca6bc597576e9e33314eb62326658"
    "c8548c22622f3a9a06b3fe40842210018bedbda8e95662a2a7bb1db9121e39bc"
    "c2f6d004c9b7cf5131d3989a32a3ade8c447d34b4244841a0cb071efdd7ed68e"
    "7c02d1aeadbda9ab1f6e5f3f2b0227e07a8d5e30418e354f000329f12f4462aa"
    "c4db0dbd034e85ed9b3dc2cac483c41bdfa690fefacfc038dd20067b1e8dc1e7"
    "72e1c719ad4e6b14c87c779b4477daa5edb47a5218ad5d4afb6983d460aa012e"
    "6415ac67135a9f9cb438ca9f9655fd2dfd44a43785d2eb7e8b4d77e81573f7f0"
    "2355f114601e9909793ec437d9d2257512c799c0ee769876bb98842995d73468"
    "e1a81ef9c8dc5e6702179d25632cfe8ec8214b76b6bceab5741ade28f5fa10ff"
    "a72abfcfaffb7e8224fc89fe65a2c440fa9a291e974f366ec5c87302e86d1df7"
    "4b73396f5e35030106dcc9ac54f20557a58dabba7975decbce146536f4a1b13d"
    "71f549e7b7d8c5e61b3c92118dbf0f6dacd09a24f3514d1642f711d02b6c29d4"
    "7f5d3f4a2b7c66dd941eb9f02ba311a72c00f449a837767c5d71e99414c84cf9"
    "e8c37ef731efaaa8266cdd309311615aea396f264389fe115abb0a1967a9af14"
    "b972f20d8bf31550d9c4e67366161b5546b58003000000000000000000000000";

PB_STATIC_ASSERT(((sizeof(k_call_data_hex) - 1U) % 2U) == 0U, CALL_DATA_HEX_MUST_HAVE_EVEN_LENGTH);

/* ---- Demo data based on Nile transaction ----
 * ffd9281d2abebb7e9ad01e9cc42411de8713258d1fc2084842d9df3759a52524
 */
const uint8_t k_owner_address[21] = {
    0x41, 0xe2, 0xf7, 0xb7, 0xe4, 0x65, 0x1c, 0x65, 0x8a, 0x8a,
    0x26, 0x7b, 0xcc, 0x70, 0xd9, 0xf0, 0x30, 0x77, 0xca, 0x1c,
    0x34};

const uint8_t k_contract_address[21] = {
    0x41, 0x34, 0x6c, 0xc7, 0xc4, 0x31, 0x2c, 0xf7, 0xa4, 0x7e,
    0xe6, 0xc5, 0x79, 0x64, 0x98, 0xce, 0x76, 0x64, 0x4e, 0x8a,
    0x85};

typedef struct
{
    const uint8_t *buf;
    size_t len;
} bytes_view_t;

typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t len;
} bytes_sink_t;

static bool encode_bytes_cb(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
    const bytes_view_t *view = (const bytes_view_t *)(*arg);
    if (view == NULL)
        return false;
    if (!pb_encode_tag_for_field(stream, field))
        return false;
    return pb_encode_string(stream, view->buf, view->len);
}

static bool decode_bytes_cb(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void)field;
    bytes_sink_t *sink = (bytes_sink_t *)(*arg);
    if (sink == NULL)
        return false;

    const size_t total = stream->bytes_left;
    sink->len = total;

    const size_t to_copy = min_size(total, sink->cap);
    if (to_copy > 0U)
    {
        if (!pb_read(stream, sink->buf, to_copy))
            return false;
    }

    size_t remaining = total - to_copy;
    while (remaining > 0U)
    {
        uint8_t scratch[32];
        const size_t chunk = min_size(remaining, sizeof(scratch));
        if (!pb_read(stream, scratch, chunk))
            return false;
        remaining -= chunk;
    }

    return true;
}

static bool hex_to_bytes_standalone(const char *hex, size_t hex_len_chars, uint8_t *out, size_t out_size)
{
    if ((hex_len_chars % 2U) != 0U)
        return false;

    const size_t needed = hex_len_chars / 2U;
    if (needed > out_size)
        return false;

    for (size_t i = 0; i < needed; i++)
    {
        const size_t pos = 2U * i;
        const int hi = hex_nibble(hex[pos]);
        const int lo = hex_nibble(hex[pos + 1U]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }

    return true;
}

static bool bytes_to_hex_standalone(const uint8_t *in, size_t in_len, char *out, size_t out_size)
{
    static const char hexmap[] = "0123456789abcdef";
    const size_t needed = in_len * 2U + 1U;
    if (out_size < needed)
        return false;
    for (size_t i = 0; i < in_len; i++)
    {
        out[i * 2U] = hexmap[in[i] >> 4];
        out[i * 2U + 1U] = hexmap[in[i] & 0x0F];
    }
    out[in_len * 2U] = '\0';
    return true;
}

static void dump_hex(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        printf("%02X ", buf[i]);
    printf("\n");
}

/* Minimal SHA-256 implementation for Base58Check (public domain style). */
typedef struct
{
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buffer[64];
    size_t buffer_len;
} sha256_ctx_t;

static uint32_t sha256_rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32U - n)); }
static uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static uint32_t sha256_ep0(uint32_t x) { return sha256_rotr(x, 2) ^ sha256_rotr(x, 13) ^ sha256_rotr(x, 22); }
static uint32_t sha256_ep1(uint32_t x) { return sha256_rotr(x, 6) ^ sha256_rotr(x, 11) ^ sha256_rotr(x, 25); }
static uint32_t sha256_sig0(uint32_t x) { return sha256_rotr(x, 7) ^ sha256_rotr(x, 18) ^ (x >> 3); }
static uint32_t sha256_sig1(uint32_t x) { return sha256_rotr(x, 17) ^ sha256_rotr(x, 19) ^ (x >> 10); }

static const uint32_t sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64])
{
    uint32_t m[64];
    for (size_t i = 0; i < 16; i++)
    {
        m[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (size_t i = 16; i < 64; i++)
    {
        m[i] = sha256_sig1(m[i - 2]) + m[i - 7] + sha256_sig0(m[i - 15]) + m[i - 16];
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (size_t i = 0; i < 64; i++)
    {
        uint32_t t1 = h + sha256_ep1(e) + sha256_ch(e, f, g) + sha256_k[i] + m[i];
        uint32_t t2 = sha256_ep0(a) + sha256_maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
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

static void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->bitlen = 0;
    ctx->buffer_len = 0;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        ctx->buffer[ctx->buffer_len++] = data[i];
        ctx->bitlen += 8;
        if (ctx->buffer_len == 64)
        {
            sha256_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32])
{
    size_t i = ctx->buffer_len;
    ctx->buffer[i++] = 0x80;
    if (i > 56)
    {
        while (i < 64)
            ctx->buffer[i++] = 0x00;
        sha256_transform(ctx, ctx->buffer);
        i = 0;
    }
    while (i < 56)
        ctx->buffer[i++] = 0x00;

    uint64_t bitlen = ctx->bitlen;
    ctx->buffer[56] = (uint8_t)(bitlen >> 56);
    ctx->buffer[57] = (uint8_t)(bitlen >> 48);
    ctx->buffer[58] = (uint8_t)(bitlen >> 40);
    ctx->buffer[59] = (uint8_t)(bitlen >> 32);
    ctx->buffer[60] = (uint8_t)(bitlen >> 24);
    ctx->buffer[61] = (uint8_t)(bitlen >> 16);
    ctx->buffer[62] = (uint8_t)(bitlen >> 8);
    ctx->buffer[63] = (uint8_t)(bitlen);
    sha256_transform(ctx, ctx->buffer);

    for (i = 0; i < 8; i++)
    {
        out[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

static void sha256_once(const uint8_t *data, size_t len, uint8_t out[32])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

static const char k_base58_alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static bool base58check_encode_tron(const uint8_t addr21[21], char *out, size_t out_size)
{
    uint8_t buf[25];
    memcpy(buf, addr21, 21);
    uint8_t h1[32];
    uint8_t h2[32];
    sha256_once(buf, 21, h1);
    sha256_once(h1, 32, h2);
    memcpy(buf + 21, h2, 4);

    /* Base58 encoding. */
    size_t zeros = 0;
    while (zeros < sizeof(buf) && buf[zeros] == 0)
        zeros++;

    uint8_t temp[35];
    size_t temp_len = 0;
    size_t start = zeros;
    while (start < sizeof(buf))
    {
        uint32_t carry = 0;
        for (size_t i = start; i < sizeof(buf); i++)
        {
            uint32_t val = (uint32_t)buf[i] + (carry << 8);
            buf[i] = (uint8_t)(val / 58);
            carry = val % 58;
        }
        temp[temp_len++] = (uint8_t)carry;
        while (start < sizeof(buf) && buf[start] == 0)
            start++;
    }

    size_t out_len = 0;
    while (out_len < zeros)
    {
        if (out_len + 1 >= out_size)
            return false;
        out[out_len++] = '1';
        zeros--;
    }
    for (size_t i = 0; i < temp_len; i++)
    {
        if (out_len + 1 >= out_size)
            return false;
        out[out_len++] = k_base58_alphabet[temp[temp_len - 1 - i]];
    }
    if (out_len >= out_size)
        return false;
    out[out_len] = '\0';
    return true;
}

int main(void)
{
    size_t required = 0;
    static const char k_custom_text[] =
        "In this section of the Developer Portal, you will find the resources to build, test and submit C and Rust apps, "
        "Ethereum plugins and Cloned coins apps, compatible with all Ledger devices (Ledger Nano S+, Ledger Nano X, Ledger "
        "Stax and Ledger Flex).This is a test case for extra data.";
    enum
    {
        CUSTOM_TEXT_LEN = (int)(sizeof(k_custom_text) - 1U),
        CUSTOM_HEX_LEN = (int)(CUSTOM_TEXT_LEN * 2U + 1U)
    };
    char custom_hex[CUSTOM_HEX_LEN];
    if (!bytes_to_hex_standalone((const uint8_t *)k_custom_text,
                                 (size_t)CUSTOM_TEXT_LEN,
                                 custom_hex,
                                 sizeof(custom_hex)))
    {
        printf("bytes_to_hex_standalone failed\n");
        return 1;
    }
    if (!tron_demo_transaction_size_with_hex_and_custom(k_call_data_hex, custom_hex, &required))
    {
        printf("sizing failed\n");
        return 1;
    }

    static uint8_t buffer[2048];
    size_t out_len = 0;
    if (!tron_demo_encode_transaction_with_hex_and_custom(buffer, sizeof(buffer), k_call_data_hex, custom_hex, &out_len))
    {
        printf("encode failed (need %zu bytes)\n", required);
        return 1;
    }

    printf("tron_demo_encode_transaction encoded %zu bytes:\n", out_len);
    dump_hex(buffer, out_len);

    /* pb_encode() reference path for byte-for-byte comparison. */
    enum { CALL_DATA_LEN = (sizeof(k_call_data_hex) - 1U) / 2U };
    static uint8_t call_data_bytes[CALL_DATA_LEN];
    if (!hex_to_bytes_standalone(k_call_data_hex, sizeof(k_call_data_hex) - 1U,
                                 call_data_bytes, sizeof(call_data_bytes)))
    {
        printf("hex_to_bytes_standalone failed\n");
        return 1;
    }

    bytes_view_t call_data_view = {call_data_bytes, sizeof(call_data_bytes)};
    protocol_TriggerSmartContract trigger_pb = protocol_TriggerSmartContract_init_zero;
    memcpy(trigger_pb.owner_address, k_owner_address, sizeof(k_owner_address));
    memcpy(trigger_pb.contract_address, k_contract_address, sizeof(k_contract_address));
    trigger_pb.call_value = 0;
    trigger_pb.data.funcs.encode = encode_bytes_cb;
    trigger_pb.data.arg = &call_data_view;

    static uint8_t trigger_buf_pb[4096];
    pb_ostream_t trigger_stream_pb = pb_ostream_from_buffer(trigger_buf_pb, sizeof(trigger_buf_pb));
    if (!pb_encode(&trigger_stream_pb, protocol_TriggerSmartContract_fields, &trigger_pb))
    {
        printf("pb_encode(TriggerSmartContract) failed: %s\n", PB_GET_ERROR(&trigger_stream_pb));
        return 1;
    }

    bytes_view_t type_url_view = {(const uint8_t *)k_type_url, strlen(k_type_url)};
    bytes_view_t any_value_view = {trigger_buf_pb, trigger_stream_pb.bytes_written};

    google_protobuf_Any any_pb = google_protobuf_Any_init_zero;
    any_pb.type_url.funcs.encode = encode_bytes_cb;
    any_pb.type_url.arg = &type_url_view;
    any_pb.value.funcs.encode = encode_bytes_cb;
    any_pb.value.arg = &any_value_view;

    protocol_Transaction tx_pb = protocol_Transaction_init_zero;
    tx_pb.has_raw_data = true;
    tx_pb.raw_data.contract_count = 1;
    tx_pb.raw_data.contract[0].type = protocol_Transaction_Contract_ContractType_TriggerSmartContract;
    tx_pb.raw_data.contract[0].has_parameter = true;
    tx_pb.raw_data.contract[0].parameter = any_pb;
    tx_pb.raw_data.fee_limit = k_fee_limit;
    bytes_view_t custom_view = {(const uint8_t *)k_custom_text, (size_t)CUSTOM_TEXT_LEN};
    tx_pb.raw_data.custom_data.funcs.encode = encode_bytes_cb;
    tx_pb.raw_data.custom_data.arg = &custom_view;

    static uint8_t buffer_pb[2048];
    pb_ostream_t tx_stream_pb = pb_ostream_from_buffer(buffer_pb, sizeof(buffer_pb));
    if (!pb_encode(&tx_stream_pb, protocol_Transaction_fields, &tx_pb))
    {
        printf("pb_encode(Transaction) failed: %s\n", PB_GET_ERROR(&tx_stream_pb));
        return 1;
    }

    const size_t pb_len = tx_stream_pb.bytes_written;
    const bool match = (out_len == pb_len) && (memcmp(buffer, buffer_pb, out_len) == 0);
    printf("pb_encode(Transaction) encoded %zu bytes: %s\n", pb_len, match ? "MATCH" : "DIFF");
    if (!match)
    {
        printf("pb_encode(Transaction) bytes:\n");
        dump_hex(buffer_pb, pb_len);
    }

    /* Stateful streaming decode: feed <=250-byte APDU-sized chunks. */
    enum { MAX_SEGMENT_SIZE = 250 };
    const size_t segment_count = (out_len + (MAX_SEGMENT_SIZE - 1U)) / MAX_SEGMENT_SIZE;

    tron_stream_decoder_t dec;
    tron_stream_decoder_init(&dec, out_len);

    size_t offset = 0;
    while (offset < out_len)
    {
        const size_t chunk = min_size(MAX_SEGMENT_SIZE, out_len - offset);
        if (!tron_stream_decoder_feed(&dec, buffer + offset, chunk))
        {
            printf("tron_stream_decoder_feed failed at offset %zu\n", offset);
            return 1;
        }
        offset += chunk;
    }

    if (!tron_stream_decoder_is_done(&dec))
    {
        printf("tron_stream_decoder did not reach DONE state\n");
        return 1;
    }

    tron_decode_result_t res;
    if (!tron_stream_decoder_get_result(&dec, &res))
    {
        printf("tron_stream_decoder_get_result failed\n");
        return 1;
    }

    printf("streaming decode used %zu segments (<=%u bytes each)\n",
           segment_count, (unsigned)MAX_SEGMENT_SIZE);
    if (res.has_contract_type)
        printf("streaming decoded contract type: %d\n", (int)res.contract_type);
    if (res.has_fee_limit)
        printf("streaming decoded fee_limit: %" PRId64 "\n", res.fee_limit);
    if (res.has_call_value)
        printf("streaming decoded call_value: %" PRId64 "\n", res.call_value);
    if (res.has_call_token_value)
        printf("streaming decoded call_token_value: %" PRId64 "\n", res.call_token_value);
    if (res.has_token_id)
        printf("streaming decoded token_id: %" PRId64 "\n", res.token_id);
    if (res.has_owner_address)
    {
        printf("streaming decoded owner_address len=%zu\n", res.owner_address_len);
        dump_hex(res.owner_address, res.owner_address_len);
        if (res.owner_address_len == 21)
        {
            char base58[64];
            if (base58check_encode_tron(res.owner_address, base58, sizeof(base58)))
                printf("owner_address (Base58): %s\n", base58);
        }
    }
    if (res.has_contract_address)
    {
        printf("streaming decoded contract_address len=%zu\n", res.contract_address_len);
        dump_hex(res.contract_address, res.contract_address_len);
        if (res.contract_address_len == 21)
        {
            char base58[64];
            if (base58check_encode_tron(res.contract_address, base58, sizeof(base58)))
                printf("contract_address (Base58): %s\n", base58);
        }
    }
    if (res.has_data)
    {
        printf("streaming decoded data len=%zu prefix_len=%zu\n",
               res.data_len, res.data_prefix_len);
        dump_hex(res.data_prefix, res.data_prefix_len);
    }
    if (res.has_custom_data)
    {
        printf("streaming decoded custom_data len=%zu prefix_len=%zu\n",
               res.custom_data_len, res.custom_data_prefix_len);
        dump_hex(res.custom_data_prefix, res.custom_data_prefix_len);
        printf("streaming decoded custom_data (prefix as text): ");
        for (size_t i = 0; i < res.custom_data_prefix_len; i++)
        {
            uint8_t c = res.custom_data_prefix[i];
            if (c >= 32 && c <= 126)
                putchar((int)c);
            else
                putchar('.');
        }
        printf("\n");
    }

    /* Compare streaming decode output with original inputs. */
    {
        bool match_all = true;
        const size_t expected_data_len = sizeof(call_data_bytes);
        const size_t expected_custom_len = (size_t)CUSTOM_TEXT_LEN;
        const size_t expected_data_prefix_len = min_size(expected_data_len, sizeof(res.data_prefix));
        const size_t expected_custom_prefix_len = min_size(expected_custom_len, sizeof(res.custom_data_prefix));

        if (!res.has_contract_type || res.contract_type != protocol_Transaction_Contract_ContractType_TriggerSmartContract)
        {
            printf("compare: contract_type mismatch\n");
            match_all = false;
        }
        if (!res.has_fee_limit || res.fee_limit != k_fee_limit)
        {
            printf("compare: fee_limit mismatch\n");
            match_all = false;
        }
        if (res.has_call_value && res.call_value != 0)
        {
            printf("compare: call_value mismatch\n");
            match_all = false;
        }
        if (res.has_call_token_value)
        {
            printf("compare: call_token_value should be absent\n");
            match_all = false;
        }
        if (res.has_token_id)
        {
            printf("compare: token_id should be absent\n");
            match_all = false;
        }
        if (!res.has_owner_address || res.owner_address_len != sizeof(k_owner_address) ||
            memcmp(res.owner_address, k_owner_address, sizeof(k_owner_address)) != 0)
        {
            printf("compare: owner_address mismatch\n");
            match_all = false;
        }
        if (!res.has_contract_address || res.contract_address_len != sizeof(k_contract_address) ||
            memcmp(res.contract_address, k_contract_address, sizeof(k_contract_address)) != 0)
        {
            printf("compare: contract_address mismatch\n");
            match_all = false;
        }
        if (!res.has_data || res.data_len != expected_data_len ||
            res.data_prefix_len != expected_data_prefix_len ||
            memcmp(res.data_prefix, call_data_bytes, res.data_prefix_len) != 0)
        {
            printf("compare: data mismatch\n");
            match_all = false;
        }
        if (!res.has_custom_data || res.custom_data_len != expected_custom_len ||
            res.custom_data_prefix_len != expected_custom_prefix_len ||
            memcmp(res.custom_data_prefix, k_custom_text, res.custom_data_prefix_len) != 0)
        {
            printf("compare: custom_data mismatch\n");
            match_all = false;
        }

        printf("streaming decode compare: %s\n", match_all ? "MATCH" : "DIFF");
    }

    /* Compare streaming decode output with pb_decode output. */
    {
        bool match_all = true;
        static uint8_t any_value_buf[4096];
        uint8_t custom_buf[CUSTOM_TEXT_LEN];

        bytes_sink_t custom_sink = {custom_buf, sizeof(custom_buf), 0};
        bytes_sink_t any_value_sink = {any_value_buf, sizeof(any_value_buf), 0};

        protocol_Transaction tx_dec = protocol_Transaction_init_zero;
        tx_dec.raw_data.custom_data.funcs.decode = decode_bytes_cb;
        tx_dec.raw_data.custom_data.arg = &custom_sink;
        tx_dec.raw_data.contract[0].parameter.value.funcs.decode = decode_bytes_cb;
        tx_dec.raw_data.contract[0].parameter.value.arg = &any_value_sink;

        pb_istream_t tx_in = pb_istream_from_buffer(buffer, out_len);
        if (!pb_decode(&tx_in, protocol_Transaction_fields, &tx_dec))
        {
            printf("pb_decode(Transaction) failed: %s\n", PB_GET_ERROR(&tx_in));
            return 1;
        }

        protocol_TriggerSmartContract trigger_dec = protocol_TriggerSmartContract_init_zero;
        bytes_sink_t data_sink = {call_data_bytes, sizeof(call_data_bytes), 0};
        trigger_dec.data.funcs.decode = decode_bytes_cb;
        trigger_dec.data.arg = &data_sink;

        pb_istream_t trigger_in = pb_istream_from_buffer(any_value_sink.buf, any_value_sink.len);
        if (!pb_decode(&trigger_in, protocol_TriggerSmartContract_fields, &trigger_dec))
        {
            printf("pb_decode(TriggerSmartContract) failed: %s\n", PB_GET_ERROR(&trigger_in));
            return 1;
        }

        if (!tx_dec.has_raw_data)
        {
            printf("pb_decode: missing raw_data\n");
            match_all = false;
        }
        if (tx_dec.raw_data.contract_count < 1)
        {
            printf("pb_decode: missing contract\n");
            match_all = false;
        }
        else if (res.has_contract_type && tx_dec.raw_data.contract[0].type != res.contract_type)
        {
            printf("pb_decode: contract_type mismatch\n");
            match_all = false;
        }

        if (res.has_fee_limit && tx_dec.raw_data.fee_limit != res.fee_limit)
        {
            printf("pb_decode: fee_limit mismatch\n");
            match_all = false;
        }

        if (res.has_call_value)
        {
            if (trigger_dec.call_value != res.call_value)
            {
                printf("pb_decode: call_value mismatch\n");
                match_all = false;
            }
        }
        else if (trigger_dec.call_value != 0)
        {
            printf("pb_decode: call_value should be absent\n");
            match_all = false;
        }

        if (res.has_call_token_value)
        {
            if (trigger_dec.call_token_value != res.call_token_value)
            {
                printf("pb_decode: call_token_value mismatch\n");
                match_all = false;
            }
        }
        else if (trigger_dec.call_token_value != 0)
        {
            printf("pb_decode: call_token_value should be absent\n");
            match_all = false;
        }

        if (res.has_token_id)
        {
            if (trigger_dec.token_id != res.token_id)
            {
                printf("pb_decode: token_id mismatch\n");
                match_all = false;
            }
        }
        else if (trigger_dec.token_id != 0)
        {
            printf("pb_decode: token_id should be absent\n");
            match_all = false;
        }

        if (res.has_owner_address &&
            memcmp(res.owner_address, trigger_dec.owner_address, res.owner_address_len) != 0)
        {
            printf("pb_decode: owner_address mismatch\n");
            match_all = false;
        }

        if (res.has_contract_address &&
            memcmp(res.contract_address, trigger_dec.contract_address, res.contract_address_len) != 0)
        {
            printf("pb_decode: contract_address mismatch\n");
            match_all = false;
        }

        if (res.has_data)
        {
            const size_t expected_prefix = min_size(data_sink.len, sizeof(res.data_prefix));
            if (res.data_len != data_sink.len ||
                res.data_prefix_len != expected_prefix ||
                memcmp(res.data_prefix, data_sink.buf, res.data_prefix_len) != 0)
            {
                printf("pb_decode: data mismatch\n");
                match_all = false;
            }
        }
        else if (data_sink.len != 0)
        {
            printf("pb_decode: data should be absent\n");
            match_all = false;
        }

        if (res.has_custom_data)
        {
            const size_t expected_prefix = min_size(custom_sink.len, sizeof(res.custom_data_prefix));
            if (res.custom_data_len != custom_sink.len ||
                res.custom_data_prefix_len != expected_prefix ||
                memcmp(res.custom_data_prefix, custom_sink.buf, res.custom_data_prefix_len) != 0)
            {
                printf("pb_decode: custom_data mismatch\n");
                match_all = false;
            }
        }
        else if (custom_sink.len != 0)
        {
            printf("pb_decode: custom_data should be absent\n");
            match_all = false;
        }

        printf("streaming decode vs pb_decode: %s\n", match_all ? "MATCH" : "DIFF");
    }

    return 0;
}
