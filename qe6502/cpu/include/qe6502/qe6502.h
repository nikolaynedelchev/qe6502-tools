#ifndef QE6502_H
#define QE6502_H

#include <stdint.h>

#ifndef QE6502_STATIC_ASSERT
# ifdef __cplusplus
#  define QE6502_STATIC_ASSERT(condition, message) static_assert((condition), message)
# else
#  define QE6502_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
# define QE6502_MAYBE_UNUSED __attribute__((unused))
#else
# define QE6502_MAYBE_UNUSED
#endif

/* Model identifiers. */
enum
{
    qe6502_model_nmos = 0,
    qe6502_model_nes  = 1,
    qe6502_model_wdc  = 2,
    qe6502_model_rw   = 3,
    qe6502_model_st   = 4,

    qe6502_supported_models_count = 5
};

/* Control-store layout.
 *
 * The control store is split into fixed-size model blocks. Each model block
 * contains 512 slots: opcode slots 0x000..0x0FF and private service slots
 * 0x100..0x1FF. Each slot contains 8 microcode entries.
 *
 * Addressing form:
 *
 *     model block + slot + cycle -> microcode entry
 */
enum
{
    /* Number of microcode entries stored in each opcode/service slot. */
    qe6502_microcode_per_slot = 8,

    /* Total number of microcode entries stored in the complete control store. */
    qe6502_control_store_size = 2 * 256 * qe6502_microcode_per_slot * qe6502_supported_models_count
};
QE6502_STATIC_ASSERT((qe6502_microcode_per_slot & (qe6502_microcode_per_slot - 1u)) == 0u,
                     "qe6502_microcode_per_slot must be a power of two");

#define QE6502_IDX(model, slot, cycle) (uint16_t)((((uint32_t)(model) & 0x0Fu) << 12u) | ((((uint32_t)(slot)) & 0x1FFu) << 3u) | (uint32_t)(cycle))
#define QE6502_SERVICE_SLOT_IDX(model, service, cycle) QE6502_IDX(model, service, cycle)

/* CPU state. */
typedef struct qe6502_cpu
{
    /* Configuration. */
    uint8_t  model;

    /* Internal execution state. */
    uint8_t  status;
    uint16_t microcode;
    uint16_t latch_addr;
    uint8_t  latch_data;

    /* Intercepts normal microcode execution. */
    uint8_t  hijack_microcode;

    /* CPU registers. */
    uint16_t PC;
    uint8_t  S;
    uint8_t  A;
    uint8_t  X;
    uint8_t  Y;
    uint8_t  P;

    /* Interrupts control. */
    uint8_t  interrupts;
} qe6502_t;
QE6502_STATIC_ASSERT(sizeof(qe6502_t) == 16, "qe6502_t must be 16 bytes");

/* Single bus-cycle result. */
typedef struct qe6502_tick_result
{
    uint16_t address;
    uint8_t  bus;
    uint8_t  status;
} qe6502_tick_t;

#define QE6502_SNAPSHOT_SIZE (64u)

enum
{
    /* Processor status register flags. */
    qe6502_flag_C  = (1u << 0),
    qe6502_flag_Z  = (1u << 1),
    qe6502_flag_I  = (1u << 2),
    qe6502_flag_D  = (1u << 3),
    qe6502_flag_B  = (1u << 4),
    qe6502_flag_UN = (1u << 5),
    qe6502_flag_V  = (1u << 6),
    qe6502_flag_N  = (1u << 7),

    /* Tick status flags. */

    /* Tick requests a memory write; clear means memory read. */
    qe6502_status_writing = (1u << 0),

    /* Tick is an opcode fetch boundary. */
    qe6502_status_opcode_fetch = (1u << 1),

    /* NMI is acknowledged by the core on this tick. */
    qe6502_status_nmi_ack = (1u << 2),

    /* IRQ is acknowledged by the core on this tick. */
    qe6502_status_irq_ack = (1u << 3),

    /* Internal reset procedure ticks. */
    qe6502_status_internal_reset = (1u << 6),

    /* CPU is jammed after a KIL opcode. */
    qe6502_status_cpu_jammed = (1u << 7),

    /* Interrupt state flags. */

    /* Interrupt sampling cycle. */
    qe6502_interrupt_sampling  = (1u << 0),

    qe6502_interrupt_nmi_inv_pin  = (1u << 1),

    qe6502_interrupt_nmi_inv_last_sampled_pin  = (1u << 2),

    qe6502_interrupt_nmi_edge = (1u << 3),

    qe6502_interrupt_nmi_taken = (1u << 4),

    qe6502_interrupt_irq_inv_pin  = (1u << 5),

    qe6502_interrupt_irq_taken  = (1u << 6),
};

/* Microcode entry. */
typedef qe6502_tick_t (*qe6502_microcode_fn)(qe6502_t *cpu, uint8_t bus);

/* Control store. */
extern const qe6502_microcode_fn qe6502_control_store[qe6502_control_store_size];

/* Public API. */

/* Restart the CPU context and return an initial dummy read request at address 0x00ff. */
qe6502_tick_t qe6502_restart(qe6502_t *cpu);

/* Enter reset-vector service and return the first bus request. */
qe6502_tick_t qe6502_reset(qe6502_t *cpu);

/* Enter execution at address and return the first bus request. */
qe6502_tick_t qe6502_goto(qe6502_t *cpu, uint16_t address);

void qe6502_nmi_assert(qe6502_t *cpu, uint8_t assert_nmi);
void qe6502_irq_assert(qe6502_t *cpu, uint8_t assert_irq);

uint8_t qe6502_is_nmi_asserted(const qe6502_t *cpu);
uint8_t qe6502_is_irq_asserted(const qe6502_t *cpu);

void qe6502_save(const qe6502_t *cpu,
                 qe6502_tick_t tick,
                 uint8_t snapshot[QE6502_SNAPSHOT_SIZE]);
qe6502_tick_t qe6502_load(qe6502_t *ctx,
                          const uint8_t snapshot[QE6502_SNAPSHOT_SIZE]);

/* Execute one CPU bus phase and return the next bus request. */
static inline QE6502_MAYBE_UNUSED
qe6502_tick_t qe6502_tick(qe6502_t *cpu, uint8_t bus)
{
    if( cpu->hijack_microcode != 0 )
    {
        return qe6502_control_store[QE6502_SERVICE_SLOT_IDX(0, 0x100, 0)](cpu, bus);
    }

    qe6502_tick_t tick = qe6502_control_store[cpu->microcode](cpu, bus);
    cpu->microcode++;
    return tick;
}


#ifdef __cplusplus
}
#endif

#endif /* QE6502_H */
