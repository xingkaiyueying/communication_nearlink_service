#pragma once
#include <cstdint>
#include <cstdlib>
struct SDF_Buff_S { uint8_t data[1600]; uint32_t size; };
inline const uint8_t *SDF_DataOffset(SDF_Buff_S *b) { return b->data; }
inline uint32_t SDF_DataLenGet(SDF_Buff_S *b) { return b->size; }
inline SDF_Buff_S *SDF_BuffNewWithReserve(uint16_t) { return new SDF_Buff_S{}; }
inline uint8_t *SDF_BuffAppend(SDF_Buff_S *b, uint16_t n) { b->size=n; return b->data; }
inline void SDF_BuffFree(SDF_Buff_S *b) { delete b; }
