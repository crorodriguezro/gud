/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Architecture-specific definitions adapted from Linux 4.9 lib/lz4.
 *
 * Copyright (C) 2013, LG Electronics, Kyungsik Lee <kyungsik.lee@lge.com>
 */
#ifndef __GUD_XDISP_LZ4DEFS_H__
#define __GUD_XDISP_LZ4DEFS_H__

#ifdef __KERNEL__
#include <asm/unaligned.h>
#include <linux/kernel.h>
#else
#include <stdint.h>
#include <string.h>
#define likely(value) (value)
#define unlikely(value) (value)
static inline uint16_t gud_get_unaligned16(const void *ptr)
{
	uint16_t value;

	memcpy(&value, ptr, sizeof(value));
	return value;
}
static inline uint32_t gud_get_unaligned32(const void *ptr)
{
	uint32_t value;

	memcpy(&value, ptr, sizeof(value));
	return value;
}
static inline uint64_t gud_get_unaligned64(const void *ptr)
{
	uint64_t value;

	memcpy(&value, ptr, sizeof(value));
	return value;
}
static inline void gud_put_unaligned16(uint16_t value, void *ptr)
{
	memcpy(ptr, &value, sizeof(value));
}
#endif

#define GUD_LZ4_BYTE u8
typedef struct {
	u16 value;
} gud_lz4_u16;
typedef struct {
	u32 value;
} gud_lz4_u32;
typedef struct {
	u64 value;
} gud_lz4_u64;

#ifdef __KERNEL__
#define GUD_LZ4_A64(ptr) get_unaligned((u64 *)(ptr))
#define GUD_LZ4_A32(ptr) get_unaligned((u32 *)(ptr))
#define GUD_LZ4_A16(ptr) get_unaligned((u16 *)(ptr))
#define GUD_LZ4_PUT16(value, ptr) put_unaligned((u16)(value), (u16 *)(ptr))
#else
#define GUD_LZ4_A64(ptr) gud_get_unaligned64(ptr)
#define GUD_LZ4_A32(ptr) gud_get_unaligned32(ptr)
#define GUD_LZ4_A16(ptr) gud_get_unaligned16(ptr)
#define GUD_LZ4_PUT16(value, ptr) gud_put_unaligned16((uint16_t)(value), ptr)
#endif

#define GUD_LZ4_PUT4(source, destination)				\
	memcpy((destination), (source), sizeof(u32))
#define GUD_LZ4_PUT8(source, destination)				\
	memcpy((destination), (source), sizeof(u64))

#define GUD_LZ4_WRITE_LE16(pointer, value)				\
	do {								\
		GUD_LZ4_PUT16((value), (pointer));			\
		(pointer) += 2;						\
	} while (0)

#define GUD_LZ4_COPY_LENGTH 8
#define GUD_LZ4_ML_BITS 4
#define GUD_LZ4_ML_MASK ((1 << GUD_LZ4_ML_BITS) - 1)
#define GUD_LZ4_RUN_BITS (8 - GUD_LZ4_ML_BITS)
#define GUD_LZ4_RUN_MASK ((1 << GUD_LZ4_RUN_BITS) - 1)
#define GUD_LZ4_MEMORY_USAGE 14
#define GUD_LZ4_MIN_MATCH 4
#define GUD_LZ4_SKIP_STRENGTH 6
#define GUD_LZ4_LAST_LITERALS 5
#define GUD_LZ4_MF_LIMIT (GUD_LZ4_COPY_LENGTH + GUD_LZ4_MIN_MATCH)
#define GUD_LZ4_MIN_LENGTH (GUD_LZ4_MF_LIMIT + 1)
#define GUD_LZ4_MAX_DISTANCE_LOG 16
#define GUD_LZ4_MAX_DISTANCE ((1 << GUD_LZ4_MAX_DISTANCE_LOG) - 1)
#define GUD_LZ4_HASH_LOG (GUD_LZ4_MAX_DISTANCE_LOG - 1)
#define GUD_LZ4_64K_LIMIT ((1 << 16) + (GUD_LZ4_MF_LIMIT - 1))
#define GUD_LZ4_HASH_LOG_64K ((GUD_LZ4_MEMORY_USAGE - 2) + 1)

#define GUD_LZ4_HASH_VALUE(pointer)					\
	((GUD_LZ4_A32(pointer) * 2654435761U) >>			\
	 ((GUD_LZ4_MIN_MATCH * 8) - (GUD_LZ4_MEMORY_USAGE - 2)))
#define GUD_LZ4_HASH64K_VALUE(pointer)					\
	((GUD_LZ4_A32(pointer) * 2654435761U) >>			\
	 ((GUD_LZ4_MIN_MATCH * 8) - GUD_LZ4_HASH_LOG_64K))

#if defined(__aarch64__) || defined(CONFIG_64BIT)
#define GUD_LZ4_STEP_SIZE 8
#define GUD_LZ4_COPY_STEP(source, destination)				\
	do {								\
		GUD_LZ4_PUT8((source), (destination));			\
		(destination) += 8;					\
		(source) += 8;						\
	} while (0)
#define GUD_LZ4_COPY_PACKET(source, destination)			\
	GUD_LZ4_COPY_STEP((source), (destination))
#define GUD_LZ4_HASH_TYPE u32
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define GUD_LZ4_COMMON_BYTES(value) (__builtin_clzll(value) >> 3)
#else
#define GUD_LZ4_COMMON_BYTES(value) (__builtin_ctzll(value) >> 3)
#endif
#else
#define GUD_LZ4_STEP_SIZE 4
#define GUD_LZ4_COPY_STEP(source, destination)				\
	do {								\
		GUD_LZ4_PUT4((source), (destination));			\
		(destination) += 4;					\
		(source) += 4;						\
	} while (0)
#define GUD_LZ4_COPY_PACKET(source, destination)			\
	do {								\
		GUD_LZ4_COPY_STEP((source), (destination));		\
		GUD_LZ4_COPY_STEP((source), (destination));		\
	} while (0)
#define GUD_LZ4_HASH_TYPE const u8 *
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define GUD_LZ4_COMMON_BYTES(value) (__builtin_clz(value) >> 3)
#else
#define GUD_LZ4_COMMON_BYTES(value) (__builtin_ctz(value) >> 3)
#endif
#endif

#define GUD_LZ4_WILD_COPY(source, destination, end)			\
	do {								\
		GUD_LZ4_COPY_PACKET((source), (destination));		\
	} while ((destination) < (end))

#define GUD_LZ4_BLIND_COPY(source, destination, length)			\
	do {								\
		u8 *gud_lz4_copy_end = (destination) + (length);		\
		GUD_LZ4_WILD_COPY((source), (destination),		\
				  gud_lz4_copy_end);			\
		(destination) = gud_lz4_copy_end;			\
	} while (0)

#endif
