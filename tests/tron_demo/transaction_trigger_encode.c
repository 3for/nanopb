#include <string.h>

#include "transaction_trigger_encode.h"

#include "pb_common.h"
#include "pb_encode.h"

#include "core/Contract.pb.h"
#include "core/Tron.pb.h"
#include "google/protobuf/any.pb.h"

/*
 * Ledger-friendly field-by-field encoding of a TRON Transaction that wraps
 * protocol.TriggerSmartContract inside google.protobuf.Any.
 *
 * Key design points for embedded targets:
 * 1) No stdio / printf.
 * 2) No large stack buffers for intermediate submessages.
 * 3) Streaming encoding from hex string for large ABI payloads.
 * 4) Provide sizing helper so caller can allocate or check buffer capacity.
 *
 * Note: Tron.options in this repo marks ref_block_*, expiration and timestamp
 * as FT_IGNORE, so this demo intentionally focuses on the represented fields.
 */

typedef bool (*encode_fn_t)(pb_ostream_t *stream, void *ctx);

static bool encode_submessage_tagged(pb_ostream_t *stream,
                                     uint32_t tag,
                                     encode_fn_t encode_fn,
                                     void *ctx)
{
    pb_ostream_t sizestream = PB_OSTREAM_SIZING;
    if (!encode_fn(&sizestream, ctx))
        return false;

    if (!pb_encode_tag(stream, PB_WT_STRING, tag))
        return false;

    if (!pb_encode_varint(stream, sizestream.bytes_written))
        return false;

    return encode_fn(stream, ctx);
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

static size_t min_size(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

/* Stream raw bytes decoded from a hex string without allocating a large buffer. */
static bool encode_hex_bytes_stream(pb_ostream_t *stream, const char *hex, size_t hex_len_chars)
{
    /* Process in small chunks to keep stack usage predictable. */
    enum { HEX_CHUNK_BYTES = 32 };
    uint8_t chunk[HEX_CHUNK_BYTES];

    if ((hex_len_chars % 2U) != 0U)
        return false;

    const size_t total_bytes = hex_len_chars / 2U;
    size_t byte_index = 0;

    while (byte_index < total_bytes)
    {
        const size_t chunk_bytes = min_size(HEX_CHUNK_BYTES, total_bytes - byte_index);

        for (size_t i = 0; i < chunk_bytes; i++)
        {
            const size_t hex_pos = 2U * (byte_index + i);
            const int hi = hex_nibble(hex[hex_pos]);
            const int lo = hex_nibble(hex[hex_pos + 1U]);
            if (hi < 0 || lo < 0)
                return false;
            chunk[i] = (uint8_t)((hi << 4) | lo);
        }

        if (!pb_write(stream, chunk, chunk_bytes))
            return false;

        byte_index += chunk_bytes;
    }

    return true;
}

static bool encode_hex_bytes_field(pb_ostream_t *stream,
                                   uint32_t tag,
                                   const char *hex,
                                   size_t hex_len_chars)
{
    if ((hex_len_chars % 2U) != 0U)
        return false;

    const size_t bytes_len = hex_len_chars / 2U;

    if (!pb_encode_tag(stream, PB_WT_STRING, tag))
        return false;
    if (!pb_encode_varint(stream, bytes_len))
        return false;

    return encode_hex_bytes_stream(stream, hex, hex_len_chars);
}

typedef struct
{
    const uint8_t *owner_address;
    size_t owner_address_len;
    const uint8_t *contract_address;
    size_t contract_address_len;
    int64_t call_value;
    const char *call_data_hex;
    size_t call_data_hex_len;
} trigger_ctx_t;

static bool encode_trigger_payload(pb_ostream_t *stream, void *vctx)
{
    trigger_ctx_t *ctx = (trigger_ctx_t *)vctx;

    if (!pb_encode_tag(stream, PB_WT_STRING, protocol_TriggerSmartContract_owner_address_tag))
        return false;
    if (!pb_encode_string(stream, ctx->owner_address, ctx->owner_address_len))
        return false;

    if (!pb_encode_tag(stream, PB_WT_STRING, protocol_TriggerSmartContract_contract_address_tag))
        return false;
    if (!pb_encode_string(stream, ctx->contract_address, ctx->contract_address_len))
        return false;

    /* proto3: default scalar values (0) are not encoded. */
    if (ctx->call_value != 0)
    {
        if (!pb_encode_tag(stream, PB_WT_VARINT, protocol_TriggerSmartContract_call_value_tag))
            return false;
        if (!pb_encode_varint(stream, (uint64_t)ctx->call_value))
            return false;
    }

    if (!encode_hex_bytes_field(stream,
                                protocol_TriggerSmartContract_data_tag,
                                ctx->call_data_hex,
                                ctx->call_data_hex_len))
        return false;

    return true;
}

typedef struct
{
    const char *type_url;
    const trigger_ctx_t *trigger;
} any_ctx_t;

static bool encode_any_payload(pb_ostream_t *stream, void *vctx)
{
    any_ctx_t *ctx = (any_ctx_t *)vctx;

    if (!pb_encode_tag(stream, PB_WT_STRING, google_protobuf_Any_type_url_tag))
        return false;
    if (!pb_encode_string(stream, (const pb_byte_t *)ctx->type_url, strlen(ctx->type_url)))
        return false;

    /* Any.value is a bytes field containing the serialized TriggerSmartContract.
     * We encode it in-place without any temporary buffers. */
    pb_ostream_t trigger_sizestream = PB_OSTREAM_SIZING;
    if (!encode_trigger_payload(&trigger_sizestream, (void *)ctx->trigger))
        return false;

    if (!pb_encode_tag(stream, PB_WT_STRING, google_protobuf_Any_value_tag))
        return false;
    if (!pb_encode_varint(stream, trigger_sizestream.bytes_written))
        return false;

    return encode_trigger_payload(stream, (void *)ctx->trigger);
}

typedef struct
{
    protocol_Transaction_Contract_ContractType type;
    any_ctx_t any_ctx;
} contract_ctx_t;

static bool encode_contract_payload(pb_ostream_t *stream, void *vctx)
{
    contract_ctx_t *ctx = (contract_ctx_t *)vctx;

    if (!pb_encode_tag(stream, PB_WT_VARINT, protocol_Transaction_Contract_type_tag))
        return false;
    if (!pb_encode_varint(stream, (uint64_t)ctx->type))
        return false;

    if (!encode_submessage_tagged(stream,
                                  protocol_Transaction_Contract_parameter_tag,
                                  encode_any_payload,
                                  &ctx->any_ctx))
        return false;

    return true;
}

typedef struct
{
    contract_ctx_t contract_ctx;
    int64_t fee_limit;
    const char *custom_data_hex;
    size_t custom_data_hex_len;
} raw_ctx_t;

static bool encode_raw_payload(pb_ostream_t *stream, void *vctx)
{
    raw_ctx_t *ctx = (raw_ctx_t *)vctx;

    if (ctx->custom_data_hex != NULL && ctx->custom_data_hex_len > 0U)
    {
        if (!encode_hex_bytes_field(stream,
                                    protocol_Transaction_raw_custom_data_tag,
                                    ctx->custom_data_hex,
                                    ctx->custom_data_hex_len))
            return false;
    }

    if (!encode_submessage_tagged(stream,
                                  protocol_Transaction_raw_contract_tag,
                                  encode_contract_payload,
                                  &ctx->contract_ctx))
        return false;

    if (!pb_encode_tag(stream, PB_WT_VARINT, protocol_Transaction_raw_fee_limit_tag))
        return false;
    if (!pb_encode_varint(stream, (uint64_t)ctx->fee_limit))
        return false;

    return true;
}

typedef struct
{
    raw_ctx_t raw_ctx;
} tx_ctx_t;

static bool encode_transaction_field_by_field(pb_ostream_t *stream, const tx_ctx_t *ctx)
{
    return encode_submessage_tagged(stream,
                                    protocol_Transaction_raw_data_tag,
                                    encode_raw_payload,
                                    (void *)&ctx->raw_ctx);
}

/* Demo addresses live in the PC demo translation unit. */
static const trigger_ctx_t k_trigger_ctx = {
    .owner_address = k_owner_address,
    .owner_address_len = sizeof(k_owner_address),
    .contract_address = k_contract_address,
    .contract_address_len = sizeof(k_contract_address),
    .call_value = 0,
    .call_data_hex = NULL,
    .call_data_hex_len = 0,
};

const char k_type_url[] = "type.googleapis.com/protocol.TriggerSmartContract";

const int64_t k_fee_limit = 200000000;

static const tx_ctx_t k_tx_ctx = {
    .raw_ctx = {
        .contract_ctx = {
            .type = protocol_Transaction_Contract_ContractType_TriggerSmartContract,
            .any_ctx = {
                .type_url = k_type_url,
                .trigger = &k_trigger_ctx,
            },
        },
        .fee_limit = k_fee_limit,
        .custom_data_hex = NULL,
        .custom_data_hex_len = 0,
    },
};

static bool is_even_hex_len(const char *hex, size_t *out_len)
{
    if (hex == NULL || out_len == NULL)
        return false;
    *out_len = strlen(hex);
    return ((*out_len % 2U) == 0U);
}

/* Public helpers suitable for embedded use. */
bool tron_demo_transaction_size_with_hex(const char *call_data_hex, size_t *out_size)
{
    if (out_size == NULL)
        return false;

    size_t hex_len = 0;
    if (!is_even_hex_len(call_data_hex, &hex_len))
        return false;

    trigger_ctx_t trigger = k_trigger_ctx;
    trigger.call_data_hex = call_data_hex;
    trigger.call_data_hex_len = hex_len;

    tx_ctx_t tx = k_tx_ctx;
    tx.raw_ctx.contract_ctx.any_ctx.trigger = &trigger;

    pb_ostream_t sizestream = PB_OSTREAM_SIZING;
    if (!encode_transaction_field_by_field(&sizestream, &tx))
        return false;

    *out_size = sizestream.bytes_written;
    return true;
}

bool tron_demo_encode_transaction_with_hex(uint8_t *out_buf,
                                            size_t out_buf_size,
                                            const char *call_data_hex,
                                            size_t *out_len)
{
    if (out_buf == NULL || out_len == NULL)
        return false;

    size_t required = 0;
    if (!tron_demo_transaction_size_with_hex(call_data_hex, &required))
        return false;

    *out_len = required;
    if (out_buf_size < required)
        return false;

    size_t hex_len = 0;
    if (!is_even_hex_len(call_data_hex, &hex_len))
        return false;

    trigger_ctx_t trigger = k_trigger_ctx;
    trigger.call_data_hex = call_data_hex;
    trigger.call_data_hex_len = hex_len;

    tx_ctx_t tx = k_tx_ctx;
    tx.raw_ctx.contract_ctx.any_ctx.trigger = &trigger;

    pb_ostream_t stream = pb_ostream_from_buffer(out_buf, out_buf_size);
    if (!encode_transaction_field_by_field(&stream, &tx))
        return false;

    /* stream.bytes_written should equal required, but return the actual value. */
    *out_len = stream.bytes_written;
    return true;
}

/* Overloads that allow injecting custom_data and call_data in raw_data. */
bool tron_demo_transaction_size_with_hex_and_custom(const char *call_data_hex,
                                                    const char *custom_data_hex,
                                                    size_t *out_size)
{
    if (out_size == NULL)
        return false;

    size_t call_len = 0;
    if (!is_even_hex_len(call_data_hex, &call_len))
        return false;

    size_t custom_len = 0;
    if (custom_data_hex != NULL && !is_even_hex_len(custom_data_hex, &custom_len))
        return false;

    trigger_ctx_t trigger = k_trigger_ctx;
    trigger.call_data_hex = call_data_hex;
    trigger.call_data_hex_len = call_len;

    tx_ctx_t tx = k_tx_ctx;
    tx.raw_ctx.contract_ctx.any_ctx.trigger = &trigger;
    tx.raw_ctx.custom_data_hex = custom_data_hex;
    tx.raw_ctx.custom_data_hex_len = custom_len;

    pb_ostream_t sizestream = PB_OSTREAM_SIZING;
    if (!encode_transaction_field_by_field(&sizestream, &tx))
        return false;

    *out_size = sizestream.bytes_written;
    return true;
}

bool tron_demo_encode_transaction_with_hex_and_custom(uint8_t *out_buf,
                                                      size_t out_buf_size,
                                                      const char *call_data_hex,
                                                      const char *custom_data_hex,
                                                      size_t *out_len)
{
    if (out_buf == NULL || out_len == NULL)
        return false;

    size_t required = 0;
    if (!tron_demo_transaction_size_with_hex_and_custom(call_data_hex, custom_data_hex, &required))
        return false;

    *out_len = required;
    if (out_buf_size < required)
        return false;

    size_t call_len = 0;
    if (!is_even_hex_len(call_data_hex, &call_len))
        return false;

    size_t custom_len = 0;
    if (custom_data_hex != NULL && !is_even_hex_len(custom_data_hex, &custom_len))
        return false;

    trigger_ctx_t trigger = k_trigger_ctx;
    trigger.call_data_hex = call_data_hex;
    trigger.call_data_hex_len = call_len;

    tx_ctx_t tx = k_tx_ctx;
    tx.raw_ctx.contract_ctx.any_ctx.trigger = &trigger;
    tx.raw_ctx.custom_data_hex = custom_data_hex;
    tx.raw_ctx.custom_data_hex_len = custom_len;

    pb_ostream_t stream = pb_ostream_from_buffer(out_buf, out_buf_size);
    if (!encode_transaction_field_by_field(&stream, &tx))
        return false;

    *out_len = stream.bytes_written;
    return true;
}
