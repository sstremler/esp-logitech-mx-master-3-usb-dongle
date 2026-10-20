#include "battery_report.h"
#include <assert.h>
#include <stddef.h>

int main(void)
{
    uint8_t buffer[3] = {0xaa, 0xbb, 0xcc};
    assert(battery_report_get(buffer, sizeof(buffer)) == 0);
    for (unsigned level = 0; level <= 100; ++level) {
        assert(battery_report_update(level));
        assert(battery_report_get(buffer, sizeof(buffer)) == 1);
        assert(buffer[0] == level);
        assert(buffer[1] == 0xbb && buffer[2] == 0xcc);
    }
    assert(battery_report_get(buffer, 0) == 0);
    assert(battery_report_get(NULL, 1) == 0);
    for (unsigned invalid = 101; invalid <= 255; ++invalid) {
        assert(!battery_report_update(invalid));
        assert(battery_report_get(buffer, sizeof(buffer)) == 0);
    }
    assert(battery_report_update(10));
    battery_report_invalidate();
    assert(battery_report_get(buffer, sizeof(buffer)) == 0);
    assert(battery_report_update(80));
    assert(battery_report_get(buffer, 1) == 1 && buffer[0] == 80);
}
