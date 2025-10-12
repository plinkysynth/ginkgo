#pragma once

static inline uint32_t undilate2(uint32_t t) {
    t = (t | (t >> 1)) & 0x33333333;
    t = (t | (t >> 2)) & 0x0F0F0F0F;
    t = (t | (t >> 4)) & 0x00FF00FF;
    t = (t | (t >> 8)) & 0x0000FFFF;
    return t;
}

static inline uint32_t dilate2(uint32_t t) {
    uint32_t r = t;
    r = (r | (r << 8)) & 0x00FF00FF;
    r = (r | (r << 4)) & 0x0F0F0F0F;
    r = (r | (r << 2)) & 0x33333333;
    r = (r | (r << 1)) & 0x55555555;
    return r;
}

static inline uint32_t morton2(uint2 c) {
    uint32_t x = dilate2(c.x);
    uint32_t y = dilate2(c.y);
    return x | (y << 1);
}

static inline uint2 unmorton2(uint32_t c) { return uint2{undilate2(c & 0x55555555), undilate2((c & 0xAAAAAAAA) >> 1)}; }

static inline uint32_t dilate3(uint32_t t) {
    uint32_t r = t;
    r = (r | (r << 16)) & 0xFF0000FF;
    r = (r | (r << 8)) & 0x0F00F00F;
    r = (r | (r << 4)) & 0xC30C30C3;
    r = (r | (r << 2)) & 0x49249249;
    return r;
}

static inline uint32_t morton(uint3 c) {
    uint32_t x = dilate3(c.x);
    uint32_t y = dilate3(c.y);
    uint32_t z = dilate3(c.z);
    return x | (y << 1) | (z << 2);
}

inline uint32_t undilate3(uint32_t t) {
    t = (t * 0x00015) & 0x0E070381;
    t = (t * 0x01041) & 0x0FF80001;
    t = (t * 0x40001) & 0x0FFC0000;

    return (t >> 18);
}


inline uint3 unmorton3(uint32_t c) {
    return uint3{undilate3(c & 0x49249249), undilate3((c & 0x92492492) >> 1), undilate3((c & 0x24924924) >> 2)};
}
