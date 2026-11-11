#include "gaia.h"

#include <stdint.h>
#include <string.h>

size_t gaia_build(uint16_t cmd, const uint8_t* payload, size_t len,
		  uint8_t* out, size_t cap) {

	if (len > 0xFFFF || cap < GAIA_HEADER_LEN + len) {
		return 0;
	}

	out[0] = 0xFF;
	out[1] = 0x03;
	out[2] = (uint8_t)(len >> 8); /* length counts payload only */
	out[3] = (uint8_t)len;
	out[4] = (uint8_t)(GAIA_VENDOR_SENNHEISER >> 8);
	out[5] = (uint8_t)(GAIA_VENDOR_SENNHEISER);
	out[6] = (uint8_t)(cmd >> 8);
	out[7] = (uint8_t)cmd;
	if (len > 0) {
		memcpy(out + GAIA_HEADER_LEN, payload, len);
	}

	return GAIA_HEADER_LEN + len;
}

int gaia_parse(const uint8_t* buf, size_t len, gaia_frame* out) {
	size_t off = 0;

	/* resync: skip anything that isn't the start of a frame */
	while (len - off >= 2 && !(buf[off] == 0xFF && buf[off + 1] == 0x03)) {
		off++;
	}

	if (len - off < GAIA_HEADER_LEN) {
		return -1; /* keep the tail, it may start a frame next read */
	}

	const size_t payload_len = ((size_t)buf[off + 2] << 8) | buf[off + 3];
	if (len - off < GAIA_HEADER_LEN + payload_len) {
		return -1;
	}

	out->vendor = (uint16_t)(buf[off + 4] << 8) | buf[off + 5];
	out->cmd = (uint16_t)(buf[off + 6] << 8) | buf[off + 7];
	out->payload = buf + off + GAIA_HEADER_LEN;
	out->payload_len = payload_len;

	return (int)(off + GAIA_HEADER_LEN + payload_len);
}

static bool is_eq_band(gaia_field field) {
	return field >= GAIA_FIELD_EQ_BAND0 &&
	       field < GAIA_FIELD_EQ_BAND0 + GAIA_EQ_BANDS;
}

size_t gaia_get_field(gaia_field field, uint8_t* out, size_t cap) {
	if (is_eq_band(field)) {
		const uint8_t band = (uint8_t)(field - GAIA_FIELD_EQ_BAND0);
		return gaia_build(GAIA_CMD_EQ_BAND_GET, &band, 1, out, cap);
	}

	switch (field) {
		case GAIA_FIELD_BATTERY:
			return gaia_build(GAIA_CMD_BATTERY_GET, NULL, 0, out,
					  cap);
		case GAIA_FIELD_ANC:
			return gaia_build(GAIA_CMD_ANC_GET, NULL, 0, out, cap);
		case GAIA_FIELD_ADAPTIVE:
			return gaia_build(GAIA_CMD_MODE_GET, NULL, 0, out, cap);
		case GAIA_FIELD_TRANSPARENCY:
			return gaia_build(GAIA_CMD_MODE_GET, NULL, 0, out, cap);
		case GAIA_FIELD_BASS:
			return gaia_build(GAIA_CMD_EQ_BASS_GET, NULL, 0, out,
					  cap);
		case GAIA_FIELD_FIRMWARE:
			return gaia_build(GAIA_CMD_FIRMWARE_GET, NULL, 0, out,
					  cap);
		default:
			return 0;
	}
}

size_t gaia_set_field(gaia_field field, int value, uint8_t* out, size_t cap) {
	uint8_t payload[2];

	if (is_eq_band(field)) {
		payload[0] = (uint8_t)(field - GAIA_FIELD_EQ_BAND0);
		payload[1] = (uint8_t)(int8_t)value; /* millibel, signed */
		return gaia_build(GAIA_CMD_EQ_BAND_SET, payload, 2, out, cap);
	}

	switch (field) {
		case GAIA_FIELD_ANC:
			payload[0] = value != 0;
			return gaia_build(GAIA_CMD_ANC_SET, payload, 1, out,
					  cap);
		case GAIA_FIELD_ADAPTIVE:
			payload[0] = GAIA_MODE_ADAPTIVE;
			payload[1] = value != 0;
			return gaia_build(GAIA_CMD_MODE_SET, payload, 2, out,
					  cap);
		case GAIA_FIELD_TRANSPARENCY:
			/* the MOMENTUM 4 has a fixed level, only an on/off mode
			 */
			payload[0] = GAIA_MODE_TRANSPARENCY;
			payload[1] = value != 0;
			return gaia_build(GAIA_CMD_MODE_SET, payload, 2, out,
					  cap);
		case GAIA_FIELD_BATTERY:
		case GAIA_FIELD_FIRMWARE:
		case GAIA_FIELD_EQ_BAND0: /* handled above */
		case GAIA_FIELD_EQ_BAND1:
		case GAIA_FIELD_EQ_BAND2:
		case GAIA_FIELD_EQ_BAND3:
		case GAIA_FIELD_EQ_BAND4:
			break;
		case GAIA_FIELD_BASS:
			payload[0] = value != 0;
			return gaia_build(GAIA_CMD_EQ_BASS_SET, payload, 1, out,
					  cap);
	}

	return 0;
}

size_t gaia_decode(const gaia_frame* f, gaia_field* fields, int* values,
		   size_t max) {
	switch (f->cmd) {
		case GAIA_CMD_BATTERY_REPLY:
			if (f->payload_len < 1 || max < 1) {
				return 0;
			}
			fields[0] = GAIA_FIELD_BATTERY;
			values[0] = f->payload[0];
			return 1;
		case GAIA_CMD_ANC_REPLY:
			if (f->payload_len < 1 || max < 1) {
				return 0;
			}
			fields[0] = GAIA_FIELD_ANC;
			values[0] = f->payload[0] != 0;
			return 1;
		case GAIA_CMD_FIRMWARE_REPLY: {
			/* three big-endian u16s: 3.38.3 -> 00 03 00 26 00 03 */
			if (f->payload_len < 6 || max < 1) {
				return 0;
			}
			const int major = (f->payload[0] << 8) | f->payload[1];
			const int minor = (f->payload[2] << 8) | f->payload[3];
			const int patch = (f->payload[4] << 8) | f->payload[5];
			fields[0] = GAIA_FIELD_FIRMWARE;
			values[0] = major * 10000 + minor * 100 + patch;
			return 1;
		}
		case GAIA_CMD_EQ_BASS_REPLY:
			if (f->payload_len < 1 || max < 1) {
				return 0;
			}
			fields[0] = GAIA_FIELD_BASS;
			values[0] = f->payload[0] != 0;
			return 1;
		case GAIA_CMD_EQ_BAND_REPLY:
			/* the gain comes back without a band index, so the
			 * session pairs it with the request it sent */
			return 0;
		case GAIA_CMD_MODE_REPLY: {
			/* (mode, state) pairs, one reply for every mode */
			size_t n = 0;
			for (size_t i = 0; i + 1 < f->payload_len && n < max;
			     i += 2) {
				switch (f->payload[i]) {
					case GAIA_MODE_TRANSPARENCY:
						fields[n] =
						    GAIA_FIELD_TRANSPARENCY;
						break;
					case GAIA_MODE_ADAPTIVE:
						fields[n] = GAIA_FIELD_ADAPTIVE;
						break;
					default:
						continue;
				}
				values[n] = f->payload[i + 1] != 0;
				n++;
			}
			return n;
		}
		default:
			return 0;
	}
}