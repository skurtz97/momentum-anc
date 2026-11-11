#ifndef GAIA_H
#define GAIA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAIA_VENDOR_SENNHEISER 0x0495
#define GAIA_HEADER_LEN 8

/* Mode ids inside the (id, value) pairs of the mode payload. */
#define GAIA_MODE_ANTI_WIND 0x01
#define GAIA_MODE_TRANSPARENCY 0x02
#define GAIA_MODE_ADAPTIVE 0x03

typedef enum gaia_cmd {
	GAIA_CMD_BATTERY_GET = 0x0603,
	GAIA_CMD_MODE_GET = 0x1A01,
	GAIA_CMD_MODE_SET = 0x1A00,
	GAIA_CMD_ANC_GET = 0x1A05,
	GAIA_CMD_ANC_SET = 0x1A04,
	/* replies repeat the request with 0x0100 set */
	GAIA_CMD_BATTERY_REPLY = 0x0703,
	GAIA_CMD_MODE_REPLY = 0x1B01,
	GAIA_CMD_ANC_REPLY = 0x1B05,
} gaia_cmd;

/* Headset properties the UI exposes. */
typedef enum gaia_field {
	GAIA_FIELD_BATTERY,
	GAIA_FIELD_ANC,
	GAIA_FIELD_ADAPTIVE,
	GAIA_FIELD_TRANSPARENCY,
} gaia_field;

typedef struct gaia_frame {
	uint16_t vendor;
	uint16_t cmd;
	const uint8_t* payload; /* points into callers buffer */
	size_t payload_len;
} gaia_frame;

/* Returns bytes written into out, or 0 if cap is too small */
size_t gaia_build(uint16_t cmd, const uint8_t* payload, size_t len,
		  uint8_t* out, size_t cap);

/* Returns bytes consumed (> 0) or -1 if no complete frame is present yet */
int gaia_parse(const uint8_t* buf, size_t len, gaia_frame* out);

/* Complete read request for field, or 0 if it has none. */
size_t gaia_get_field(gaia_field field, uint8_t* out, size_t cap);

/* Complete write request for field, or 0 if read-only or out of range. */
size_t gaia_set_field(gaia_field field, int value, uint8_t* out, size_t cap);

/* Decodes a reply into up to max field updates, returns how many (0 for acks
 * and frames we do not track). A mode reply carries several fields at once. */
size_t gaia_decode(const gaia_frame* f, gaia_field* fields, int* values,
		   size_t max);

#endif // GAIA_H
