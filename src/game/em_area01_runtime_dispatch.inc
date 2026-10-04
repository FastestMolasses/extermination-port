/* Address dispatch into existing translations; shared originals stay in the host. */
    case 0x001B13F0u:
        if (c->na < 2 || c->nf < 1) return fail(r, c->function);
        rc = em_area01_math_001B13F0(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1], c->f[0], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001B2140u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_math_001B2140(&r->math, (uint32_t)c->a[0], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001C25E0u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_math_001C25E0(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1]);
        break;
    case 0x001C2770u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_math_001C2770(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001C39F0u:
        if (c->na < 2 || c->nf < 1) return fail(r, c->function);
        rc = em_area01_math_001C39F0(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1], c->f[0]);
        break;
    case 0x001C3BE0u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_math_001C3BE0(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1]);
        break;
    case 0x001C3D60u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_math_001C3D60(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1]);
        break;
    case 0x001BB860u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_math_001BB860(&r->math, (uint32_t)c->a[0]);
        break;
    case 0x001BB560u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_math_001BB560(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001C02E0u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_math_001C02E0(&r->math, (uint32_t)c->a[0]);
        break;
    case 0x001BF630u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_math_001BF630(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001BFFD0u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_math_001BFFD0(&r->math, (uint32_t)c->a[0]);
        break;
    case 0x001B9CF0u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_math_001B9CF0(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001BBAE0u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_math_001BBAE0(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001BBBF0u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_math_001BBBF0(&r->math, (uint32_t)c->a[0], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001D0C80u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_math_001D0C80(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001D0D40u:
        if (c->na < 4) return fail(r, c->function);
        rc = em_area01_math_001D0D40(&r->math, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], (uint32_t)c->a[3]);
        break;
    case 0x001287F0u:
        if (c->na < 3 || c->nf < 1) return fail(r, c->function);
        rc = em_area01_sys_001287F0(&r->sys, (uint32_t)c->a[0], (uint32_t)c->a[1], (int32_t)c->a[2], c->f[0]);
        break;
    case 0x00128B80u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_sys_00128B80(&r->sys, (uint32_t)c->a[0], (uint32_t)c->a[1], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x00128C10u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_sys_00128C10(&r->sys, (uint32_t)c->a[0]);
        break;
    case 0x00157CE0u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_sys_00157CE0(&r->sys, (uint32_t)c->a[0], (int32_t)c->a[1], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x00158590u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_sys_00158590(&r->sys, (uint32_t)c->a[0], (int32_t)c->a[1], (int32_t)c->a[2]);
        break;
    case 0x00158D30u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_sys_00158D30(&r->sys, (uint32_t)c->a[0]);
        break;
    case 0x00159B90u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_sys_00159B90(&r->sys, (uint32_t)c->a[0]);
        break;
    case 0x0015A2C0u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_sys_0015A2C0(&r->sys, (uint32_t)c->a[0]);
        break;
    case 0x0019B4C0u:
        if (c->na < 4) return fail(r, c->function);
        rc = em_area01_sys_0019B4C0(&r->sys, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], (int32_t)c->a[3], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x0019CF50u:
        rc = em_area01_sys_0019CF50(&r->sys, &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001A06A0u:
        rc = em_area01_sys_001A06A0(&r->sys, &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001A8840u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_sys_001A8840(&r->sys, (uint32_t)c->a[0], (uint32_t)c->a[1]);
        break;
    case 0x001A9E00u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_sys_001A9E00(&r->sys, (uint32_t)c->a[0], (uint32_t)c->a[1]);
        break;
    case 0x001AA000u:
        if (c->na < 4) return fail(r, c->function);
        rc = em_area01_sys_001AA000(&r->sys, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2],
                                    (uint32_t)c->a[3]);
        break;
    case 0x001B0300u:
        rc = em_area01_sys_001B0300(&r->sys);
        break;
    case 0x001B0D80u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_sys_001B0D80(&r->sys, (uint32_t)c->a[0], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001B6D70u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_sys_001B6D70(&r->sys, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001B76D0u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_sys_001B76D0(&r->sys, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001E3D90u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_sys_001E3D90(&r->sys, (uint32_t)c->a[0]);
        break;
    case 0x001E7C60u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_sys_001E7C60(&r->sys, (uint32_t)c->a[0], (uint32_t)c->a[1]);
        break;
    case 0x001E7CB0u:
        rc = em_area01_sys_001E7CB0(&r->sys, &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001E7D20u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_sys_001E7D20(&r->sys, (uint32_t)c->a[0]);
        break;
    case 0x00113478u:
        rc = em_area01_exita_00113478(&r->exita, &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001195A8u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exita_001195A8(&r->exita, (uint32_t)c->a[0], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x00128390u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_exita_00128390(&r->exita, (uint32_t)c->a[0], (int32_t)c->a[1], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x00128600u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exita_00128600(&r->exita, (int32_t)c->a[0], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x00128640u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exita_00128640(&r->exita, (uint32_t)c->a[0], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001289C0u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_exita_001289C0(&r->exita, (uint32_t)c->a[0], (uint32_t)c->a[1]);
        break;
    case 0x00128AB0u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_exita_00128AB0(&r->exita, (uint32_t)c->a[0], (uint32_t)c->a[1], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x00129780u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_exita_00129780(&r->exita, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x0012A5D0u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exita_0012A5D0(&r->exita, (uint32_t)c->a[0]);
        break;
    case 0x0012ADC0u:
        if (c->na < 3 || c->nf < 1) return fail(r, c->function);
        rc = em_area01_exita_0012ADC0(&r->exita, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], c->f[0], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x0012AFC0u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_exita_0012AFC0(&r->exita, (uint32_t)c->a[0], (uint32_t)c->a[1]);
        break;
    case 0x0022DCD0u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exita_0022DCD0(&r->exita, (uint32_t)c->a[0]);
        break;
    case 0x00156F30u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exitb_00156F30(&r->exitb, (uint32_t)c->a[0]);
        break;
    case 0x001576E0u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_exitb_001576E0(&r->exitb, (uint32_t)c->a[0], (uint32_t)c->a[1], &i32); if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001581A0u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exitb_001581A0(&r->exitb, (uint32_t)c->a[0]);
        break;
    case 0x00158810u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exitb_00158810(&r->exitb, (uint32_t)c->a[0]);
        break;
    case 0x00158BD0u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exitb_00158BD0(&r->exitb, (uint32_t)c->a[0]);
        break;
    case 0x0015AB00u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exitb_0015AB00(&r->exitb, (uint32_t)c->a[0]);
        break;
    case 0x0015B030u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exitb_0015B030(&r->exitb, (uint32_t)c->a[0]);
        break;
    case 0x001BB520u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exitb_001BB520(&r->exitb, (uint32_t)c->a[0], &u64); if (rc == 0) c->v0 = u64;
        break;
    case 0x001C2430u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_exitb_001C2430(&r->exitb, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], &u64); if (rc == 0) c->v0 = u64;
        break;
    case 0x001C2540u:
        if (c->na < 4) return fail(r, c->function);
        rc = em_area01_exitb_001C2540(&r->exitb, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], (uint32_t)c->a[3], &u64); if (rc == 0) c->v0 = u64;
        break;
    case 0x001C3DB0u:
        if (c->na < 4) return fail(r, c->function);
        rc = em_area01_exitb_001C3DB0(&r->exitb, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], (uint32_t)c->a[3]);
        break;
    case 0x001C6160u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exitb_001C6160(&r->exitb, (uint32_t)c->a[0], &u32); if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001D0400u:
        if (c->na < 2 || c->nf < 1) return fail(r, c->function);
        rc = em_area01_exitb_001D0400(&r->exitb, (uint32_t)c->a[0], (uint32_t)c->a[1], c->f[0]);
        break;
    case 0x001E8E80u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_exitb_001E8E80(&r->exitb, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2]);
        break;
    case 0x001E9280u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_exitb_001E9280(&r->exitb, (uint32_t)c->a[0]);
        break;
    case 0x001E9580u:
        if (c->na < 3) return fail(r, c->function);
        rc = em_area01_exitb_001E9580(&r->exitb, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2]);
        break;
