/* em_area01_math_core.c - EE storage view and callee dispatch for the AREA01
 * lane "math" translations (see em_area01_math_core.h, docs/AREA01_MATH.md). */
#include "game/em_area01_math_core.h"

#include <stddef.h>

static void fault(EmA01Math *m, uint32_t address, int32_t code)
{
    if (m->fault_code == EM_A01M_FAULT_NONE) {
        m->fault_code = code;
        m->fault_address = address;
    }
}

void em_area01_math_clear_fault(EmA01Math *m)
{
    m->fault_code = EM_A01M_FAULT_NONE;
    m->fault_address = 0;
}

static uint8_t *where_rw(EmA01Math *m, uint32_t a, uint32_t size)
{
    if (m->fault_code != EM_A01M_FAULT_NONE) return NULL;
    if (a >= EM_A01M_SPAD_BASE && a - EM_A01M_SPAD_BASE <= EM_A01M_SPAD_SIZE - size) {
        if (!m->spad) { fault(m, a, EM_A01M_FAULT_NULL); return NULL; }
        return m->spad + (a - EM_A01M_SPAD_BASE);
    }
    uint32_t base = 0;
    if (a >= EM_A01M_ACCEL) base = EM_A01M_ACCEL;
    else if (a >= EM_A01M_UNCACHED) base = EM_A01M_UNCACHED;
    uint32_t off = a - base;
    if (m->ram_size >= size && off <= m->ram_size - size && (base == 0 || a < base + 0x10000000u)) {
        if (!m->ram) { fault(m, a, EM_A01M_FAULT_NULL); return NULL; }
        return m->ram + off;
    }
    fault(m, a, EM_A01M_FAULT_ADDRESS);
    return NULL;
}

const uint8_t *em_a01m_where(EmA01Math *m, uint32_t a, uint32_t size) { return where_rw(m, a, size); }

static uint32_t load(EmA01Math *m, uint32_t a, uint32_t size)
{
    const uint8_t *p = em_a01m_where(m, a, size);
    uint32_t v = 0;
    if (!p) return 0;
    for (uint32_t i = 0; i < size; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static void store(EmA01Math *m, uint32_t a, uint32_t v, uint32_t size)
{
    uint8_t *p = where_rw(m, a, size);
    if (!p) return;
    for (uint32_t i = 0; i < size; i++) p[i] = (uint8_t)(v >> (8 * i));
    m->stores++;
    if (m->trace && m->trace_len != UINT32_MAX) {
        if (m->trace_len <= m->trace_cap && m->trace_cap - m->trace_len >= 2) {
            m->trace[m->trace_len] = a;
            m->trace[m->trace_len + 1] = size;
            m->trace_len += 2;
        } else {
            m->trace_len = UINT32_MAX;
        }
    }
}

uint32_t em_a01m_lw(EmA01Math *m, uint32_t a) { return load(m, a, 4); }
uint32_t em_a01m_lhu(EmA01Math *m, uint32_t a) { return load(m, a, 2); }
int32_t em_a01m_lh(EmA01Math *m, uint32_t a) { return (int32_t)(int16_t)(uint16_t)load(m, a, 2); }
uint32_t em_a01m_lbu(EmA01Math *m, uint32_t a) { return load(m, a, 1); }
int32_t em_a01m_lb(EmA01Math *m, uint32_t a) { return (int32_t)(int8_t)(uint8_t)load(m, a, 1); }
void em_a01m_sw(EmA01Math *m, uint32_t a, uint32_t v) { store(m, a, v, 4); }
void em_a01m_sh(EmA01Math *m, uint32_t a, uint32_t v) { store(m, a, v, 2); }
void em_a01m_sb(EmA01Math *m, uint32_t a, uint32_t v) { store(m, a, v, 1); }

int em_a01m_call(EmA01Math *m, uint32_t target, const uint32_t *a, unsigned na, const uint32_t *f,
                 unsigned nf, uint32_t *v0, uint32_t *f0)
{
    uint32_t rv = 0, rf = 0;
    if (m->fault_code != EM_A01M_FAULT_NONE) return -1;
    if (!m->call) { fault(m, target, EM_A01M_FAULT_NULL); return -1; }
    if (m->call(m->ctx, target, a, na, f, nf, &rv, &rf) < 0) {
        fault(m, target, EM_A01M_FAULT_WORKER);
        return -1;
    }
    if (v0) *v0 = rv;
    if (f0) *f0 = rf;
    return 0;
}

int em_a01m_call_i(EmA01Math *m, uint32_t target, unsigned na, uint32_t a0, uint32_t a1, uint32_t a2,
                   uint32_t a3, uint32_t *v0)
{
    const uint32_t a[4] = {a0, a1, a2, a3};
    return em_a01m_call(m, target, a, na, NULL, 0, v0, NULL);
}

int em_a01m_call_if(EmA01Math *m, uint32_t target, unsigned na, uint32_t a0, uint32_t a1, uint32_t a2,
                    uint32_t a3, uint32_t f12, uint32_t *v0, uint32_t *f0)
{
    const uint32_t a[4] = {a0, a1, a2, a3};
    const uint32_t f[1] = {f12};
    return em_a01m_call(m, target, a, na, f, 1, v0, f0);
}
