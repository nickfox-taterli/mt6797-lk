/* SPDX-License-Identifier: MIT */
#include <x20/boot.h>
#include <lib/miniz.h>

/* Exactly one gzip member. consumed identifies the appended DTB without
 * looking for magic inside compressed data or reading beyond the entry.
 */
int gzip_decode(const void *src, size_t len, void *dst, size_t cap,
                size_t *consumed, size_t *written)
{
    const uint8_t *p = src;
    tinfl_decompressor state = {0};
    size_t pos = 10, input, output = cap;
    if (!src || !dst || !consumed || !written || len < 18 ||
        p[0] != 0x1f || p[1] != 0x8b || p[2] != 8 || (p[3] & 0xe0))
        return E_GZIP;
    if (p[3] & 4) {
        if (len - pos < 2) return E_GZIP;
        size_t n = p[pos] | (size_t)p[pos + 1] << 8;
        pos += 2;
        if (n > len - pos) return E_GZIP;
        pos += n;
    }
    for (unsigned flag = 8; flag <= 16; flag <<= 1) {
        if (p[3] & flag) {
            while (pos < len && p[pos]) pos++;
            if (pos == len) return E_GZIP;
            pos++;
        }
    }
    if (p[3] & 2) {
        if (len - pos < 2 || (x20_crc32(p, pos) & 0xffff) !=
            (uint32_t)(p[pos] | p[pos + 1] << 8)) return E_GZIP;
        pos += 2;
    }
    if (len - pos < 8) return E_GZIP;
    input = len - pos;
    tinfl_init(&state);
    tinfl_status status = tinfl_decompress(&state, p + pos, &input,
        dst, dst, &output, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    /* This pinned tinfl keeps look-ahead bytes in its bit buffer at DONE.
     * Return complete unused bytes before locating the gzip trailer.
     */
    if (status == TINFL_STATUS_DONE) {
        size_t ahead = state.m_num_bits / 8;
        if (ahead > input) return E_GZIP;
        input -= ahead;
    }
    if (status != TINFL_STATUS_DONE || input > len - pos ||
        len - pos - input < 8 || !output)
        return E_GZIP;
    pos += input;
    if (get_le32(p + pos) != x20_crc32(dst, output) ||
        get_le32(p + pos + 4) != output) return E_GZIP;
    *consumed = pos + 8;
    *written = output;
    return 0;
}
