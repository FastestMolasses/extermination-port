/* Canonical address-view contract; all fixture bytes are synthetic. */
#include "game/em_area01_math_core.h"
#include "game/em_area01_math_owner.h"
#include "game/em_ee_float.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t actor[0x100], record[0x20], resource[8], scratch[16];
    unsigned reads, writes, workers;
} Fixture;

static uint8_t *span(uint32_t a, uint32_t n, uint32_t base, uint8_t *p, uint32_t size)
{
    return a >= base && n <= size && a - base <= size - n ? p + a - base : NULL;
}

static uint8_t *view(void *ctx, uint32_t a, uint32_t n, int write)
{
    Fixture *f = ctx;
    uint8_t *p;
    if (write) ++f->writes; else ++f->reads;
    if ((p = span(a, n, 0x1000, f->actor, sizeof f->actor))) return p;
    if ((p = span(a, n, 0x2000, f->record, sizeof f->record))) return p;
    if ((p = span(a, n, 0x70003FF0, f->scratch, sizeof f->scratch))) return p;
    return write ? NULL : span(a, n, 0x3000, f->resource, sizeof f->resource);
}

static int worker(void *ctx, uint32_t target, const uint32_t *a, unsigned na,
                  const uint32_t *f, unsigned nf, uint32_t *v0, uint32_t *f0)
{
    Fixture *fixture = ctx;
    assert(target == 0x1234 && na == 1 && a[0] == 0x2000 && nf == 0);
    (void)f; (void)f0;
    ++fixture->workers;
    fixture->record[0] = 0xA5;
    *v0 = 7;
    return 0;
}

int main(void)
{
    Fixture f = {0};
    uint32_t trace[32] = {0};
    EmA01Math m = {.ctx = &f, .call = worker, .view = view,
                   .trace = trace, .trace_cap = 32};
    em_a01m_sw(&m, 0x1090, 0x2000);
    assert(em_area01_math_001D0D40(&m, 0x1000, 0x12345678, 0x80000001, 0x1FF) == 0);
    assert(em_a01m_lw(&m, 0x2000) == 0x12345678);
    assert(em_a01m_lw(&m, 0x2004) == em_ee_cvt_s_w_bits(0x80000001));
    assert(em_a01m_lw(&m, 0x2008) == 0 && f.record[12] == 0xFF);
    assert(em_a01m_where(&m, 0x2000, 0x20) == f.record);
    assert(m.stores == 5 && m.trace_len == 10);
    assert(trace[0] == 0x1090 && trace[1] == 4 && trace[8] == 0x200C && trace[9] == 1);

    em_a01m_sh(&m, 0x2000200E, 0xBEEF);
    assert(em_a01m_lhu(&m, 0x3000200E) == 0xBEEF);
    assert(f.record[14] == 0xEF && f.record[15] == 0xBE);
    assert(trace[10] == 0x2000200E && trace[11] == 2);
    em_a01m_sw(&m, 0x70003FFC, 0xAABBCCDD);
    assert(em_a01m_lw(&m, 0x70003FFC) == 0xAABBCCDD);
    f.resource[0] = 0x42;
    assert(em_a01m_lbu(&m, 0x3000) == 0x42);
    uint32_t result = 0;
    assert(em_a01m_call_i(&m, 0x1234, 1, 0x2000, 0, 0, 0, &result) == 0);
    assert(result == 7 && em_a01m_lbu(&m, 0x2000) == 0xA5 && f.workers == 1);

    /* A refused write preserves the owner and latches the original address. */
    unsigned stores = m.stores;
    em_a01m_sb(&m, 0x20003000, 0x99);
    assert(m.fault_code == EM_A01M_FAULT_ADDRESS && m.fault_address == 0x20003000);
    assert(f.resource[0] == 0x42 && m.stores == stores);
    unsigned maps = f.reads + f.writes;
    em_a01m_sw(&m, 0x2000, 0);
    assert(em_a01m_lw(&m, 0x2000) == 0);
    assert(em_a01m_call_i(&m, 0x1234, 1, 0x2000, 0, 0, 0, &result) == -1);
    assert(f.reads + f.writes == maps && f.workers == 1 && f.record[0] == 0xA5);
    const uint32_t bad[] = {0x201E, 0x70003FFE, 0x40002000, 0xFFFFFFFF};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        em_area01_math_clear_fault(&m);
        assert(em_a01m_where(&m, bad[i], 4) == NULL);
        assert(m.fault_code == EM_A01M_FAULT_ADDRESS && m.fault_address == bad[i]);
    }
    em_area01_math_clear_fault(&m);
    assert(em_a01m_where(&m, 0x70000000, UINT32_MAX) == NULL);
    assert(em_a01m_where(NULL, 0x1000, 4) == NULL);

    /* The linear fixture path still aliases all three RAM windows. */
    uint8_t linear[16] = {0};
    EmA01Math raw = {.ram = linear, .ram_size = sizeof linear};
    em_a01m_sw(&raw, 0x20000004, 0x76543210);
    assert(em_a01m_lw(&raw, 0x30000004) == 0x76543210);
    assert(em_a01m_where(&raw, 4, 4) == linear + 4);
    puts("area01 math views: PASS canonical aliases, real helper writes, worker visibility, read-only spans, bounds and fault latch");
    return 0;
}
