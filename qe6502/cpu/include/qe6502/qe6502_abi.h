/*
 * MIT License
 *
 * Copyright (c) 2025 Nikolay Nedelchev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#ifndef QE6502_ABI_H
#define QE6502_ABI_H

#include <stdint.h>

#if defined(__cplusplus)
#   define QE6502_ABI_ALIGNAS(n) alignas(n)
#   define QE6502_ABI_ALIGNOF(type) alignof(type)
#   define QE6502_ABI_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#   include <stdalign.h>
#   define QE6502_ABI_ALIGNAS(n) alignas(n)
#   define QE6502_ABI_ALIGNOF(type) alignof(type)
#   define QE6502_ABI_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

#if defined(QE6502_STATIC)
#   define QE6502_ABI_API
#elif defined(_WIN32)
#   if defined(QE6502_BUILDING_LIBRARY)
#       define QE6502_ABI_API __declspec(dllexport)
#   elif defined(QE6502_SHARED)
#       define QE6502_ABI_API __declspec(dllimport)
#   else
#       define QE6502_ABI_API
#   endif
#elif defined(__GNUC__) || defined(__clang__)
#   define QE6502_ABI_API __attribute__((visibility("default")))
#else
#   define QE6502_ABI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stable shared/WASM/FFI ABI wrapper.
 *
 * This ABI deliberately exposes only scalar values and caller-owned opaque
 * storage. It does not perform null-pointer, bounds, model, or state-safety
 * checks; callers must pass valid objects and must not edit context bytes.
 */

#define QE6502_ABI_VERSION_MAJOR 0u
#define QE6502_ABI_VERSION_MINOR 3u
#define QE6502_ABI_VERSION \
    ((QE6502_ABI_VERSION_MAJOR << 16u) | QE6502_ABI_VERSION_MINOR)

#define QE6502_ABI_CONTEXT_SIZE    64u
#define QE6502_ABI_CONTEXT_ALIGN   8u
#define QE6502_ABI_CONTEXT_MAGIC   0x6502u
#define QE6502_ABI_CONTEXT_VERSION 1u

#define QE6502_ABI_SNAPSHOT_SIZE QE6502_ABI_CONTEXT_SIZE

/*
 * Opaque 64-byte, 8-byte-aligned ABI context.
 * The first 20 bytes are currently used; the remaining 44 bytes are reserved.
 */
typedef struct qe6502abi_context
{
    QE6502_ABI_ALIGNAS(QE6502_ABI_CONTEXT_ALIGN)
    uint8_t bytes[QE6502_ABI_CONTEXT_SIZE];
} qe6502abi_context_t;

QE6502_ABI_STATIC_ASSERT(sizeof(qe6502abi_context_t) == QE6502_ABI_CONTEXT_SIZE,
                         "qe6502abi_context_t must be exactly 64 bytes");
QE6502_ABI_STATIC_ASSERT(QE6502_ABI_ALIGNOF(qe6502abi_context_t) == QE6502_ABI_CONTEXT_ALIGN,
                         "qe6502abi_context_t must be 8-byte aligned");

typedef uint32_t qe6502abi_tick_t;

#define QE6502_ABI_MODEL_NMOS  0u
#define QE6502_ABI_MODEL_NES   1u
#define QE6502_ABI_MODEL_WDC   2u
#define QE6502_ABI_MODEL_RW    3u
#define QE6502_ABI_MODEL_ST    4u
#define QE6502_ABI_MODEL_COUNT 5u

#define QE6502_ABI_FLAG_C  (1u << 0u)
#define QE6502_ABI_FLAG_Z  (1u << 1u)
#define QE6502_ABI_FLAG_I  (1u << 2u)
#define QE6502_ABI_FLAG_D  (1u << 3u)
#define QE6502_ABI_FLAG_B  (1u << 4u)
#define QE6502_ABI_FLAG_UN (1u << 5u)
#define QE6502_ABI_FLAG_V  (1u << 6u)
#define QE6502_ABI_FLAG_N  (1u << 7u)

#define QE6502_ABI_STATUS_WRITING    (1u << 0u)
#define QE6502_ABI_STATUS_FETCH      (1u << 1u)
#define QE6502_ABI_STATUS_NMI_ACK    (1u << 2u)
#define QE6502_ABI_STATUS_IRQ_ACK    (1u << 3u)
#define QE6502_ABI_STATUS_CPU_JAMMED (1u << 7u)

/* Packed tick layout: address bits 0..15, bus bits 16..23, status bits 24..31. */
#define QE6502_ABI_TICK_ADDRESS_SHIFT 0u
#define QE6502_ABI_TICK_BUS_SHIFT     16u
#define QE6502_ABI_TICK_STATUS_SHIFT  24u

#define QE6502_ABI_TICK_WRITING_SHIFT    24u
#define QE6502_ABI_TICK_FETCH_SHIFT      25u
#define QE6502_ABI_TICK_NMI_ACK_SHIFT    26u
#define QE6502_ABI_TICK_IRQ_ACK_SHIFT    27u
#define QE6502_ABI_TICK_CPU_JAMMED_SHIFT 31u

#define QE6502_ABI_TICK_WRITING    (UINT32_C(1) << QE6502_ABI_TICK_WRITING_SHIFT)
#define QE6502_ABI_TICK_FETCH      (UINT32_C(1) << QE6502_ABI_TICK_FETCH_SHIFT)
#define QE6502_ABI_TICK_NMI_ACK    (UINT32_C(1) << QE6502_ABI_TICK_NMI_ACK_SHIFT)
#define QE6502_ABI_TICK_IRQ_ACK    (UINT32_C(1) << QE6502_ABI_TICK_IRQ_ACK_SHIFT)
#define QE6502_ABI_TICK_CPU_JAMMED (UINT32_C(1) << QE6502_ABI_TICK_CPU_JAMMED_SHIFT)

#define QE6502_ABI_TICK_ADDRESS(tick) \
    ((uint16_t)(((uint32_t)(tick) >> QE6502_ABI_TICK_ADDRESS_SHIFT) & 0xffffu))
#define QE6502_ABI_TICK_BUS(tick) \
    ((uint8_t)(((uint32_t)(tick) >> QE6502_ABI_TICK_BUS_SHIFT) & 0xffu))
#define QE6502_ABI_TICK_STATUS(tick) \
    ((uint8_t)(((uint32_t)(tick) >> QE6502_ABI_TICK_STATUS_SHIFT) & 0xffu))

/* ABI version. */
QE6502_ABI_API uint32_t qe6502abi_version(void);

/* Primary CPU control path. */
QE6502_ABI_API void             qe6502abi_setup(qe6502abi_context_t *ctx, uint32_t model);
QE6502_ABI_API qe6502abi_tick_t qe6502abi_restart(qe6502abi_context_t *ctx);
QE6502_ABI_API qe6502abi_tick_t qe6502abi_tick(qe6502abi_context_t *ctx, uint32_t bus);

/* Additional execution control. */
QE6502_ABI_API qe6502abi_tick_t qe6502abi_reset(qe6502abi_context_t *ctx);
QE6502_ABI_API qe6502abi_tick_t qe6502abi_goto(qe6502abi_context_t *ctx, uint32_t address);
QE6502_ABI_API void             qe6502abi_nmi(qe6502abi_context_t *ctx);
QE6502_ABI_API void             qe6502abi_set_irq(qe6502abi_context_t *ctx, uint32_t pin);
QE6502_ABI_API uint32_t         qe6502abi_get_irq(const qe6502abi_context_t *ctx);

/* Execution functions return ticks but do not store them in the ABI context. */

/*
 * Save and restore the portable 64-byte ABI context snapshot.
 * Snapshot layout is big-endian: magic, version, CPU context, tick, reserved.
 */
QE6502_ABI_API void             qe6502abi_save(const qe6502abi_context_t *ctx,
                                               qe6502abi_tick_t tick,
                                               uint8_t snapshot[QE6502_ABI_SNAPSHOT_SIZE]);
QE6502_ABI_API qe6502abi_tick_t qe6502abi_load(qe6502abi_context_t *ctx,
                                               const uint8_t snapshot[QE6502_ABI_SNAPSHOT_SIZE]);

/* Debug/helper accessors for public CPU state. */
QE6502_ABI_API uint32_t qe6502abi_get_pc(const qe6502abi_context_t *ctx);
QE6502_ABI_API void     qe6502abi_set_pc(qe6502abi_context_t *ctx, uint32_t value);
QE6502_ABI_API uint32_t qe6502abi_get_s(const qe6502abi_context_t *ctx);
QE6502_ABI_API void     qe6502abi_set_s(qe6502abi_context_t *ctx, uint32_t value);
QE6502_ABI_API uint32_t qe6502abi_get_a(const qe6502abi_context_t *ctx);
QE6502_ABI_API void     qe6502abi_set_a(qe6502abi_context_t *ctx, uint32_t value);
QE6502_ABI_API uint32_t qe6502abi_get_x(const qe6502abi_context_t *ctx);
QE6502_ABI_API void     qe6502abi_set_x(qe6502abi_context_t *ctx, uint32_t value);
QE6502_ABI_API uint32_t qe6502abi_get_y(const qe6502abi_context_t *ctx);
QE6502_ABI_API void     qe6502abi_set_y(qe6502abi_context_t *ctx, uint32_t value);
QE6502_ABI_API uint32_t qe6502abi_get_p(const qe6502abi_context_t *ctx);
QE6502_ABI_API void     qe6502abi_set_p(qe6502abi_context_t *ctx, uint32_t value);
QE6502_ABI_API uint32_t qe6502abi_get_model(const qe6502abi_context_t *ctx);
QE6502_ABI_API void     qe6502abi_set_model(qe6502abi_context_t *ctx, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* QE6502_ABI_H */
