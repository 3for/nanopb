#ifndef TRON_TRANSACTION_TRIGGER_ENCODE_H
#define TRON_TRANSACTION_TRIGGER_ENCODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t k_owner_address[21];
extern const uint8_t k_contract_address[21];
extern const char k_type_url[];
extern const int64_t k_fee_limit;

bool tron_demo_transaction_size_with_hex(const char *call_data_hex, size_t *out_size);
bool tron_demo_encode_transaction_with_hex(uint8_t *out_buf,
                                            size_t out_buf_size,
                                            const char *call_data_hex,
                                            size_t *out_len);

bool tron_demo_transaction_size_with_hex_and_custom(const char *call_data_hex,
                                                    const char *custom_data_hex,
                                                    size_t *out_size);
bool tron_demo_encode_transaction_with_hex_and_custom(uint8_t *out_buf,
                                                      size_t out_buf_size,
                                                      const char *call_data_hex,
                                                      const char *custom_data_hex,
                                                      size_t *out_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
