#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DONGLE_BATTERY_REPORT_ID 3

/* Generic Device Controls / Battery Strength, one byte in percent.
 * Linux hid-input recognizes this usage as a device power supply. */
#define DONGLE_BATTERY_REPORT_DESCRIPTOR \
    0x85, DONGLE_BATTERY_REPORT_ID, /* Report ID */ \
    0x05, 0x06,                   /* Usage Page: Generic Device Controls */ \
    0x09, 0x20,                   /* Usage: Battery Strength */ \
    0x15, 0x00,                   /* Logical Minimum: 0 */ \
    0x25, 0x64,                   /* Logical Maximum: 100 */ \
    0x75, 0x08,                   /* Report Size: 8 */ \
    0x95, 0x01,                   /* Report Count: 1 */ \
    0xb1, 0x02                    /* Feature: Data, Variable, Absolute */

bool battery_report_update(uint8_t percent);
void battery_report_invalidate(void);
/* Returns payload length, excluding the report ID, or zero when unavailable. */
uint16_t battery_report_get(uint8_t *buffer, uint16_t reqlen);
