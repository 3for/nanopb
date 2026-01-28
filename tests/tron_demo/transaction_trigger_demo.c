#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

    if (!encode_submessage_tagged(stream,
                                  protocol_Transaction_raw_contract_tag,
                                  encode_contract_payload,
                                  &ctx->contract_ctx))
        return false;

    if (ctx->custom_data_hex != NULL && ctx->custom_data_hex_len > 0U)
    {
        if (!encode_hex_bytes_field(stream,
                                    protocol_Transaction_raw_custom_data_tag,
                                    ctx->custom_data_hex,
                                    ctx->custom_data_hex_len))
            return false;
    }

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

/* ---------------- Streaming / APDU-friendly decoder ----------------
 *
 * Nanopb's pb_decode() cannot pause/resume across multiple APDU frames.
 * The following minimal wire-format state machine can be fed chunk-by-chunk
 * without requiring a full transaction buffer in RAM.
 *
 * It only parses the fields we care about for this demo:
 * - Transaction.raw_data.contract[0].type
 * - Transaction.raw_data.fee_limit
 * - TriggerSmartContract.{owner_address, contract_address, call_value}
 */
typedef enum
{
    TRON_CTX_TX = 0,
    TRON_CTX_RAW = 1,
    TRON_CTX_CONTRACT = 2,
    TRON_CTX_ANY = 3,
    TRON_CTX_TRIGGER = 4
} tron_ctx_t;

typedef struct
{
    tron_ctx_t ctx;
    size_t remaining;
} tron_frame_t;

typedef enum
{
    TRON_MODE_KEY = 0,
    TRON_MODE_VARINT = 1,
    TRON_MODE_LENGTH = 2,
    TRON_MODE_BYTES = 3
} tron_mode_t;

typedef enum
{
    TRON_ACT_SKIP = 0,
    TRON_ACT_ENTER_RAW = 1,
    TRON_ACT_ENTER_CONTRACT = 2,
    TRON_ACT_ENTER_ANY = 3,
    TRON_ACT_ENTER_TRIGGER = 4
} tron_action_t;

typedef struct
{
    bool has_contract_type;
    protocol_Transaction_Contract_ContractType contract_type;

    bool has_fee_limit;
    int64_t fee_limit;

    bool has_call_value;
    int64_t call_value;

    bool has_call_token_value;
    int64_t call_token_value;

    bool has_token_id;
    int64_t token_id;

    bool has_owner_address;
    uint8_t owner_address[21];
    size_t owner_address_len;

    bool has_contract_address;
    uint8_t contract_address[21];
    size_t contract_address_len;

    bool has_data;
    size_t data_len;
    uint8_t data_prefix[32];
    size_t data_prefix_len;

    bool has_custom_data;
    size_t custom_data_len;
    uint8_t custom_data_prefix[32];
    size_t custom_data_prefix_len;
} tron_decode_result_t;

typedef struct
{
    /* Frame stack tracks nested length-delimited messages. */
    tron_frame_t frames[5];
    size_t depth;

    /* Parser mode and pending field information. */
    tron_mode_t mode;
    tron_action_t pending_action;
    uint32_t pending_tag;
    pb_wire_type_t pending_wire;

    /* Incremental varint parser state. */
    uint64_t varint_value;
    uint8_t varint_shift;
    uint8_t varint_count;

    /* Byte skipping / capture state. */
    size_t bytes_remaining;
    uint8_t *capture_buf;
    size_t capture_cap;
    size_t capture_len;

    /* Error / completion state. */
    bool error;
    bool done;

    tron_decode_result_t result;
} tron_stream_decoder_t;

static tron_ctx_t tron_current_ctx(const tron_stream_decoder_t *dec)
{
    if (dec->depth == 0)
        return TRON_CTX_TX;
    return dec->frames[dec->depth - 1U].ctx;
}

static void tron_set_error(tron_stream_decoder_t *dec)
{
    dec->error = true;
}

static bool tron_push_frame(tron_stream_decoder_t *dec, tron_ctx_t ctx, size_t len)
{
    if (dec->depth >= (sizeof(dec->frames) / sizeof(dec->frames[0])))
        return false;
    dec->frames[dec->depth].ctx = ctx;
    dec->frames[dec->depth].remaining = len;
    dec->depth++;
    return true;
}

static void tron_pop_finished_frames(tron_stream_decoder_t *dec)
{
    while (dec->depth > 0 && dec->frames[dec->depth - 1U].remaining == 0U)
    {
        dec->depth--;
    }
    dec->done = (dec->depth == 0U);
}

/* Each consumed byte reduces the remaining count of all active frames. */
static bool tron_consume_byte(tron_stream_decoder_t *dec)
{
    if (dec->done || dec->depth == 0U)
        return false;

    for (size_t i = 0; i < dec->depth; i++)
    {
        if (dec->frames[i].remaining == 0U)
            return false;
        dec->frames[i].remaining--;
    }

    return true;
}

static void tron_varint_reset(tron_stream_decoder_t *dec)
{
    dec->varint_value = 0;
    dec->varint_shift = 0;
    dec->varint_count = 0;
}

static bool tron_varint_feed(tron_stream_decoder_t *dec, uint8_t byte, bool *out_done, uint64_t *out_value)
{
    if (dec->varint_count >= 10U)
        return false;

    dec->varint_value |= ((uint64_t)(byte & 0x7FU)) << dec->varint_shift;
    dec->varint_shift = (uint8_t)(dec->varint_shift + 7U);
    dec->varint_count++;

    if ((byte & 0x80U) == 0U)
    {
        *out_done = true;
        *out_value = dec->varint_value;
        tron_varint_reset(dec);
    }
    else
    {
        *out_done = false;
    }

    return true;
}

static tron_action_t tron_length_action(tron_ctx_t ctx, uint32_t tag, pb_wire_type_t wire)
{
    if (wire != PB_WT_STRING)
        return TRON_ACT_SKIP;

    switch (ctx)
    {
    case TRON_CTX_TX:
        return (tag == protocol_Transaction_raw_data_tag) ? TRON_ACT_ENTER_RAW : TRON_ACT_SKIP;
    case TRON_CTX_RAW:
        return (tag == protocol_Transaction_raw_contract_tag) ? TRON_ACT_ENTER_CONTRACT : TRON_ACT_SKIP;
    case TRON_CTX_CONTRACT:
        return (tag == protocol_Transaction_Contract_parameter_tag) ? TRON_ACT_ENTER_ANY : TRON_ACT_SKIP;
    case TRON_CTX_ANY:
        return (tag == google_protobuf_Any_value_tag) ? TRON_ACT_ENTER_TRIGGER : TRON_ACT_SKIP;
    case TRON_CTX_TRIGGER:
        /* owner_address / contract_address will be captured in-place. */
        return TRON_ACT_SKIP;
    default:
        return TRON_ACT_SKIP;
    }
}

static void tron_handle_varint_value(tron_stream_decoder_t *dec, uint64_t value)
{
    const tron_ctx_t ctx = tron_current_ctx(dec);

    if (ctx == TRON_CTX_RAW && dec->pending_tag == protocol_Transaction_raw_fee_limit_tag)
    {
        dec->result.has_fee_limit = true;
        dec->result.fee_limit = (int64_t)value;
    }
    else if (ctx == TRON_CTX_CONTRACT && dec->pending_tag == protocol_Transaction_Contract_type_tag)
    {
        dec->result.has_contract_type = true;
        dec->result.contract_type = (protocol_Transaction_Contract_ContractType)value;
    }
    else if (ctx == TRON_CTX_TRIGGER && dec->pending_tag == protocol_TriggerSmartContract_call_value_tag)
    {
        dec->result.has_call_value = true;
        dec->result.call_value = (int64_t)value;
    }
    else if (ctx == TRON_CTX_TRIGGER && dec->pending_tag == protocol_TriggerSmartContract_call_token_value_tag)
    {
        dec->result.has_call_token_value = true;
        dec->result.call_token_value = (int64_t)value;
    }
    else if (ctx == TRON_CTX_TRIGGER && dec->pending_tag == protocol_TriggerSmartContract_token_id_tag)
    {
        dec->result.has_token_id = true;
        dec->result.token_id = (int64_t)value;
    }
}

static void tron_start_bytes(tron_stream_decoder_t *dec, size_t len)
{
    dec->bytes_remaining = len;
    dec->mode = (len == 0U) ? TRON_MODE_KEY : TRON_MODE_BYTES;
}

static void tron_start_capture_if_needed(tron_stream_decoder_t *dec, size_t len)
{
    const tron_ctx_t ctx = tron_current_ctx(dec);
    if (dec->pending_wire != PB_WT_STRING)
        return;

    if (ctx == TRON_CTX_RAW && dec->pending_tag == protocol_Transaction_raw_custom_data_tag)
    {
        dec->result.has_custom_data = true;
        dec->result.custom_data_len = len;
        dec->capture_buf = dec->result.custom_data_prefix;
        dec->capture_cap = sizeof(dec->result.custom_data_prefix);
        dec->capture_len = 0;
        dec->result.custom_data_prefix_len = min_size(len, dec->capture_cap);
        return;
    }

    if (ctx != TRON_CTX_TRIGGER)
        return;

    if (dec->pending_tag == protocol_TriggerSmartContract_owner_address_tag)
    {
        dec->capture_buf = dec->result.owner_address;
        dec->capture_cap = sizeof(dec->result.owner_address);
        dec->capture_len = 0;
        dec->result.has_owner_address = true;
        dec->result.owner_address_len = min_size(len, dec->capture_cap);
    }
    else if (dec->pending_tag == protocol_TriggerSmartContract_contract_address_tag)
    {
        dec->capture_buf = dec->result.contract_address;
        dec->capture_cap = sizeof(dec->result.contract_address);
        dec->capture_len = 0;
        dec->result.has_contract_address = true;
        dec->result.contract_address_len = min_size(len, dec->capture_cap);
    }
    else if (dec->pending_tag == protocol_TriggerSmartContract_data_tag)
    {
        dec->result.has_data = true;
        dec->result.data_len = len;
        dec->capture_buf = dec->result.data_prefix;
        dec->capture_cap = sizeof(dec->result.data_prefix);
        dec->capture_len = 0;
        dec->result.data_prefix_len = min_size(len, dec->capture_cap);
    }
}

static bool tron_enter_submessage(tron_stream_decoder_t *dec, tron_action_t action, size_t len)
{
    tron_ctx_t next_ctx;
    switch (action)
    {
    case TRON_ACT_ENTER_RAW:
        next_ctx = TRON_CTX_RAW;
        break;
    case TRON_ACT_ENTER_CONTRACT:
        next_ctx = TRON_CTX_CONTRACT;
        break;
    case TRON_ACT_ENTER_ANY:
        next_ctx = TRON_CTX_ANY;
        break;
    case TRON_ACT_ENTER_TRIGGER:
        next_ctx = TRON_CTX_TRIGGER;
        break;
    default:
        return false;
    }

    if (!tron_push_frame(dec, next_ctx, len))
        return false;

    /* If len is zero, we immediately pop and continue. */
    tron_pop_finished_frames(dec);
    return true;
}

static bool tron_process_length(tron_stream_decoder_t *dec, size_t len)
{
    const tron_action_t action = tron_length_action(tron_current_ctx(dec), dec->pending_tag, dec->pending_wire);

    if (action == TRON_ACT_ENTER_RAW || action == TRON_ACT_ENTER_CONTRACT ||
        action == TRON_ACT_ENTER_ANY || action == TRON_ACT_ENTER_TRIGGER)
    {
        dec->mode = TRON_MODE_KEY;
        return tron_enter_submessage(dec, action, len);
    }

    /* Otherwise treat it as a bytes field and skip/capture. */
    dec->capture_buf = NULL;
    dec->capture_cap = 0;
    dec->capture_len = 0;
    tron_start_capture_if_needed(dec, len);
    tron_start_bytes(dec, len);
    return true;
}

static bool tron_process_byte(tron_stream_decoder_t *dec, uint8_t byte)
{
    if (!tron_consume_byte(dec))
        return false;

    bool ok = true;
    bool done = false;
    uint64_t value = 0;

    switch (dec->mode)
    {
    case TRON_MODE_KEY:
        if (!tron_varint_feed(dec, byte, &done, &value))
        {
            ok = false;
            break;
        }
        if (done)
        {
            dec->pending_tag = (uint32_t)(value >> 3U);
            dec->pending_wire = (pb_wire_type_t)(value & 0x07U);

            switch (dec->pending_wire)
            {
            case PB_WT_VARINT:
                dec->mode = TRON_MODE_VARINT;
                break;
            case PB_WT_STRING:
                dec->mode = TRON_MODE_LENGTH;
                break;
            case PB_WT_32BIT:
                dec->pending_action = TRON_ACT_SKIP;
                tron_start_bytes(dec, 4U);
                break;
            case PB_WT_64BIT:
                dec->pending_action = TRON_ACT_SKIP;
                tron_start_bytes(dec, 8U);
                break;
            default:
                ok = false;
                break;
            }
        }
        break;

    case TRON_MODE_VARINT:
        if (!tron_varint_feed(dec, byte, &done, &value))
        {
            ok = false;
            break;
        }
        if (done)
        {
            tron_handle_varint_value(dec, value);
            dec->mode = TRON_MODE_KEY;
        }
        break;

    case TRON_MODE_LENGTH:
        if (!tron_varint_feed(dec, byte, &done, &value))
        {
            ok = false;
            break;
        }
        if (done)
        {
            ok = tron_process_length(dec, (size_t)value);
        }
        break;

    case TRON_MODE_BYTES:
        if (dec->bytes_remaining == 0U)
        {
            dec->mode = TRON_MODE_KEY;
            break;
        }

        if (dec->capture_buf != NULL && dec->capture_len < dec->capture_cap)
        {
            dec->capture_buf[dec->capture_len++] = byte;
        }

        dec->bytes_remaining--;
        if (dec->bytes_remaining == 0U)
        {
            if (dec->capture_buf == dec->result.data_prefix)
            {
                dec->result.data_prefix_len = dec->capture_len;
            }
            else if (dec->capture_buf == dec->result.custom_data_prefix)
            {
                dec->result.custom_data_prefix_len = dec->capture_len;
            }
            dec->mode = TRON_MODE_KEY;
        }
        break;

    default:
        ok = false;
        break;
    }

    if (ok)
    {
        /* Pop frames only after processing the current byte in its context. */
        tron_pop_finished_frames(dec);
    }

    return ok;
}

void tron_stream_decoder_init(tron_stream_decoder_t *dec, size_t total_len)
{
    memset(dec, 0, sizeof(*dec));
    dec->mode = TRON_MODE_KEY;
    tron_varint_reset(dec);
    dec->done = (total_len == 0U);

    if (!dec->done)
    {
        (void)tron_push_frame(dec, TRON_CTX_TX, total_len);
    }
}

bool tron_stream_decoder_feed(tron_stream_decoder_t *dec, const uint8_t *data, size_t len)
{
    if (dec == NULL || data == NULL || dec->error || dec->done)
        return false;

    for (size_t i = 0; i < len; i++)
    {
        if (!tron_process_byte(dec, data[i]))
        {
            tron_set_error(dec);
            return false;
        }

        if (dec->done)
        {
            /* Extra bytes beyond the declared total length are not allowed. */
            return (i + 1U == len);
        }
    }

    return true;
}

bool tron_stream_decoder_is_done(const tron_stream_decoder_t *dec)
{
    return dec != NULL && dec->done && !dec->error;
}

bool tron_stream_decoder_get_result(const tron_stream_decoder_t *dec, tron_decode_result_t *out)
{
    if (dec == NULL || out == NULL || dec->error || !dec->done)
        return false;
    *out = dec->result;
    return true;
}

/* ---- Demo data based on Nile transaction ----
 * ffd9281d2abebb7e9ad01e9cc42411de8713258d1fc2084842d9df3759a52524
 */
static const uint8_t k_owner_address[21] = {
    0x41, 0xe2, 0xf7, 0xb7, 0xe4, 0x65, 0x1c, 0x65, 0x8a, 0x8a,
    0x26, 0x7b, 0xcc, 0x70, 0xd9, 0xf0, 0x30, 0x77, 0xca, 0x1c,
    0x34};

static const uint8_t k_contract_address[21] = {
    0x41, 0x34, 0x6c, 0xc7, 0xc4, 0x31, 0x2c, 0xf7, 0xa4, 0x7e,
    0xe6, 0xc5, 0x79, 0x64, 0x98, 0xce, 0x76, 0x64, 0x4e, 0x8a,
    0x85};

/* Large ABI payload kept as hex string in flash; decoded on the fly. */
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

static const trigger_ctx_t k_trigger_ctx = {
    .owner_address = k_owner_address,
    .owner_address_len = sizeof(k_owner_address),
    .contract_address = k_contract_address,
    .contract_address_len = sizeof(k_contract_address),
    .call_value = 0,
    .call_data_hex = NULL,
    .call_data_hex_len = 0,
};

static const char k_type_url[] = "type.googleapis.com/protocol.TriggerSmartContract";

static const tx_ctx_t k_tx_ctx = {
    .raw_ctx = {
        .contract_ctx = {
            .type = protocol_Transaction_Contract_ContractType_TriggerSmartContract,
            .any_ctx = {
                .type_url = k_type_url,
                .trigger = &k_trigger_ctx,
            },
        },
        .fee_limit = 200000000,
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

/* Backwards-compatible wrappers for the built-in demo data. */
bool tron_demo_transaction_size(size_t *out_size)
{
    return tron_demo_transaction_size_with_hex(k_call_data_hex, out_size);
}

bool tron_demo_encode_transaction(uint8_t *out_buf, size_t out_buf_size, size_t *out_len)
{
    return tron_demo_encode_transaction_with_hex(out_buf, out_buf_size, k_call_data_hex, out_len);
}

/* Overloads that allow injecting custom_data in raw_data. */
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

#ifdef TRON_DEMO_STANDALONE
#include <inttypes.h>
#include <stdio.h>
#include "pb_decode.h"

typedef struct
{
    const uint8_t *buf;
    size_t len;
} bytes_view_t;

static bool encode_bytes_cb(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
    const bytes_view_t *view = (const bytes_view_t *)(*arg);
    if (view == NULL)
        return false;
    if (!pb_encode_tag_for_field(stream, field))
        return false;
    return pb_encode_string(stream, view->buf, view->len);
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
    const char *custom_text = "This is a test case for extra data";
    char custom_hex[128];
    if (!bytes_to_hex_standalone((const uint8_t *)custom_text,
                                 strlen(custom_text),
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
    tx_pb.raw_data.fee_limit = k_tx_ctx.raw_ctx.fee_limit;

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
    return 0;
}
#endif
