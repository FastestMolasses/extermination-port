#ifndef EM_AIM_FIRE_TABLES_H
#define EM_AIM_FIRE_TABLES_H
#include <stdint.h>
#define EM_AIM_FIRE_TABLE_BASE 0x00248680u
#define EM_AIM_FIRE_TABLE_SIZE 0x680u
#define EM_AIM_FIRE_TABLE_PATH "assets/aim_fire_tables.emaf"
/* Read-only ELF tables, exported locally; no original bytes in the port. */
int em_aim_fire_tables_load(void);
const uint8_t *em_aim_fire_tables_bytes(uint32_t address, uint32_t size);
/* True if any byte of the request belongs to a readonly table window.
 * Independent of whether the user's export has been loaded. */
int em_aim_fire_tables_contains(uint32_t address,uint32_t size);
#endif
