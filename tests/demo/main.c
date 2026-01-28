#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "pb_common.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "demo.pb.h"

/* ---------- 工具：按 tag 查找字段描述符（nanopb 0.4+ / 1.0+ 风格） ---------- */
static bool find_field_by_tag(pb_field_iter_t *iter,
                              const pb_msgdesc_t *msg,
                              void *msg_struct,
                              uint32_t tag)
{
    if (!pb_field_iter_begin(iter, msg, msg_struct)) {
        return false;
    }
    return pb_field_iter_find(iter, tag);
}

/* ---------- 工具：编码一个 string（nanopb string = length-delimited） ---------- */
static bool encode_string_field(pb_ostream_t *s, const pb_field_iter_t *f, const char *str)
{
    if (!pb_encode_tag_for_field(s, f)) return false;
    return pb_encode_string(s, (const pb_byte_t*)str, strlen(str));
}

/* ---------- 工具：编码一个 bytes ---------- */
static bool encode_bytes_field(pb_ostream_t *s, const pb_field_iter_t *f, const uint8_t *buf, size_t n)
{
    if (!pb_encode_tag_for_field(s, f)) return false;
    return pb_encode_string(s, (const pb_byte_t*)buf, n);
}

/* ---------- 工具：编码一个 uint32（varint） ---------- */
static bool encode_u32_field(pb_ostream_t *s, const pb_field_iter_t *f, uint32_t v)
{
    if (!pb_encode_tag_for_field(s, f)) return false;
    return pb_encode_varint(s, v);
}

/* ---------- 工具：编码 packed repeated uint32 ---------- */
static bool encode_packed_u32_field(pb_ostream_t *s,
                                    const pb_field_iter_t *f,
                                    const uint32_t *values,
                                    size_t count)
{
    pb_ostream_t sizestream = PB_OSTREAM_SIZING;
    for (size_t i = 0; i < count; i++) {
        if (!pb_encode_varint(&sizestream, values[i])) return false;
    }

    if (!pb_encode_tag(s, PB_WT_STRING, f->tag)) return false;
    if (!pb_encode_varint(s, sizestream.bytes_written)) return false;

    for (size_t i = 0; i < count; i++) {
        if (!pb_encode_varint(s, values[i])) return false;
    }

    return true;
}

/* ---------- 工具：编码一个“子消息”字段（length-delimited 的 submessage） ---------- */
static bool encode_submessage_field(pb_ostream_t *s,
                                   const pb_field_iter_t *f,
                                   const pb_msgdesc_t *sub_fields,
                                   const void *sub_struct_ptr)
{
    /* 这里为了示例简单：子消息我们用“正常填 struct + pb_encode_submessage()”。
       你也可以对 Sub 再做一份“逐字段编码”的版本（见下方注释）。 */
    if (!pb_encode_tag_for_field(s, f)) return false;
    return pb_encode_submessage(s, sub_fields, sub_struct_ptr);
}

/* ---------- 逐字段编码 Demo（不构造 Demo 结构体） ---------- */
static bool encode_demo_field_by_field(pb_ostream_t *s)
{
    /* 通过 tag 找字段描述符（也可以直接用 fields 数组的下标，但不建议写死） */
    Demo dummy = Demo_init_zero;
    pb_field_iter_t f_id, f_name, f_nums, f_payload, f_sub;

    bool ok =
        find_field_by_tag(&f_id, Demo_fields, &dummy, Demo_id_tag) &&
        find_field_by_tag(&f_name, Demo_fields, &dummy, Demo_name_tag) &&
        find_field_by_tag(&f_nums, Demo_fields, &dummy, Demo_nums_tag) &&
        find_field_by_tag(&f_payload, Demo_fields, &dummy, Demo_payload_tag) &&
        find_field_by_tag(&f_sub, Demo_fields, &dummy, Demo_sub_tag);

    if (!ok) {
        return false;
    }

    /* 1) id = 123 */
    if (!encode_u32_field(s, &f_id, 123)) return false;

    /* 2) name = "alice" */
    if (!encode_string_field(s, &f_name, "alice")) return false;

    /* 3) nums = [7, 8, 9]  —— 与 pb_encode() 保持一致：packed */
    uint32_t nums[] = {7, 8, 9};
    if (!encode_packed_u32_field(s, &f_nums, nums, sizeof(nums)/sizeof(nums[0]))) return false;

    /* 4) payload = 0xDE 0xAD 0xBE 0xEF */
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    if (!encode_bytes_field(s, &f_payload, payload, sizeof(payload))) return false;

    /* 5) sub = { x: 42, note: "hi" } */
    Sub sub = Sub_init_zero;
    sub.x = 42;
    /* nanopb 的 string 在 proto3 默认是 pb_callback_t 还是 pb_size_t+char[] 取决于 .options；
       为了通用，这里假设生成的是 pb_callback_t 或者静态数组都能用？
       最稳妥：给 Sub.note 配成静态数组（见下方“注意事项”）。
       这里先按“静态数组”写： */
    strncpy(sub.note, "hi", sizeof(sub.note) - 1);
    sub.note[sizeof(sub.note) - 1] = '\0';

    if (!encode_submessage_field(s, &f_sub, Sub_fields, &sub)) return false;

    return true;
}

int main(void)
{
    uint8_t buffer[256];
    pb_ostream_t os = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!encode_demo_field_by_field(&os)) {
        fprintf(stderr, "encode failed: %s\n", PB_GET_ERROR(&os));
        return 1;
    }

    size_t n = os.bytes_written;
    printf("encoded %zu bytes:\n", n);
    for (size_t i = 0; i < n; i++) printf("%02X ", buffer[i]);
    printf("\n");

    /* ---------- 用常规 pb_encode() 编码并对比结果 ---------- */
    uint8_t buffer2[256];
    pb_ostream_t os2 = pb_ostream_from_buffer(buffer2, sizeof(buffer2));

    Demo msg2 = Demo_init_zero;
    msg2.id = 123;
    strncpy(msg2.name, "alice", sizeof(msg2.name) - 1);
    msg2.name[sizeof(msg2.name) - 1] = '\0';

    msg2.nums_count = 3;
    msg2.nums[0] = 7;
    msg2.nums[1] = 8;
    msg2.nums[2] = 9;

    msg2.payload.size = 4;
    msg2.payload.bytes[0] = 0xDE;
    msg2.payload.bytes[1] = 0xAD;
    msg2.payload.bytes[2] = 0xBE;
    msg2.payload.bytes[3] = 0xEF;

    msg2.has_sub = true;
    msg2.sub.x = 42;
    strncpy(msg2.sub.note, "hi", sizeof(msg2.sub.note) - 1);
    msg2.sub.note[sizeof(msg2.sub.note) - 1] = '\0';

    if (!pb_encode(&os2, Demo_fields, &msg2)) {
        fprintf(stderr, "pb_encode failed: %s\n", PB_GET_ERROR(&os2));
        return 1;
    }

    size_t n2 = os2.bytes_written;
    bool same = (n == n2) && (memcmp(buffer, buffer2, n) == 0);
    printf("pb_encode encoded %zu bytes: %s\n", n2, same ? "MATCH" : "DIFF");
    if (!same) {
        printf("pb_encode bytes:\n");
        for (size_t i = 0; i < n2; i++) printf("%02X ", buffer2[i]);
        printf("\n");
    }

    /* ---------- 用普通 pb_decode 校验 ---------- */
    Demo msg = Demo_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buffer, n);

    if (!pb_decode(&is, Demo_fields, &msg)) {
        fprintf(stderr, "decode failed: %s\n", PB_GET_ERROR(&is));
        return 1;
    }

    printf("decoded:\n");
    printf("  id=%" PRIu32 "\n", msg.id);
    printf("  name=%s\n", msg.name);

    /* repeated nums：如果你用的是静态数组模式，会有 nums_count + nums[] */
    printf("  nums_count=%zu\n", (size_t)msg.nums_count);
    for (size_t i = 0; i < msg.nums_count; i++) {
        printf("    nums[%zu]=%" PRIu32 "\n", i, msg.nums[i]);
    }

    printf("  payload_size=%zu\n", (size_t)msg.payload.size);
    printf("  payload=");
    for (size_t i = 0; i < msg.payload.size; i++) printf("%02X ", msg.payload.bytes[i]);
    printf("\n");

    printf("  sub.x=%" PRIu32 "\n", msg.sub.x);
    printf("  sub.note=%s\n", msg.sub.note);

    return 0;
}
