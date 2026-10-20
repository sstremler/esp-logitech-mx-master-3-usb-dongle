#include "battery_report.h"
#include <stdatomic.h>
#include <stddef.h>

/* The NimBLE and TinyUSB tasks can run on different cores. A single atomic
 * value keeps validity and capacity consistent without blocking USB control. */
static atomic_uint battery_percent = ATOMIC_VAR_INIT(101);

bool battery_report_update(uint8_t percent)
{
    atomic_store(&battery_percent, percent <= 100 ? percent : 101);
    return percent <= 100;
}

void battery_report_invalidate(void)
{
    atomic_store(&battery_percent, 101);
}

uint16_t battery_report_get(uint8_t *buffer, uint16_t reqlen)
{
    unsigned percent = atomic_load(&battery_percent);
    if (buffer == NULL || reqlen == 0 || percent > 100) {
        return 0;
    }
    buffer[0] = (uint8_t)percent;
    return 1;
}
