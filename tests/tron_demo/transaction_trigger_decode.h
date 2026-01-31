#ifndef TRON_TRANSACTION_TRIGGER_DECODE_H
#define TRON_TRANSACTION_TRIGGER_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pb.h"
#include "core/Tron.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    tron_frame_t frames[5];
    size_t depth;

    tron_mode_t mode;
    tron_action_t pending_action;
    uint32_t pending_tag;
    pb_wire_type_t pending_wire;

    uint64_t varint_value;
    uint8_t varint_shift;
    uint8_t varint_count;

    size_t bytes_remaining;
    uint8_t *capture_buf;
    size_t capture_cap;
    size_t capture_len;

    bool error;
    bool done;

    tron_decode_result_t result;
} tron_stream_decoder_t;

void tron_stream_decoder_init(tron_stream_decoder_t *dec, size_t total_len);
bool tron_stream_decoder_feed(tron_stream_decoder_t *dec, const uint8_t *data, size_t len);
bool tron_stream_decoder_is_done(const tron_stream_decoder_t *dec);
bool tron_stream_decoder_get_result(const tron_stream_decoder_t *dec, tron_decode_result_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
