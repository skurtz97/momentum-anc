#include "gaia.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t BATTERY_REPLY[] = {
    0xFF, 0x03, 0x00, 0x01, 0x04, 0x95, 0x07, 0x03, 0x64,
};
static const uint8_t MODE_REPLY[] = {0xFF, 0x03, 0x00, 0x06, 0x04, 0x95, 0x1B,
				     0x01, 0x01, 0x02, 0x02, 0x00, 0x03, 0x01};

int main(void) {
	uint8_t out[32];
	struct gaia_frame f;

	/* build: GET battery is exactly 8 bytes */
	assert(gaia_build(GAIA_CMD_BATTERY_GET, NULL, 0, out, sizeof out) == 8);
	assert(memcmp(out, "\xFF\x03\x00\x00\x04\x95\x06\x03", 8) == 0);

	/* build: set adaptive = [mode 3, state 1] -> 10 bytes */
	const uint8_t set_adaptive[] = {0x03, 0x01};
	assert(gaia_build(GAIA_CMD_MODE_SET, set_adaptive, 2, out,
			  sizeof out) == 10);
	assert(out[2] == 0x00 && out[3] == 0x02); /* length is payload only */

	/* parse: real responses */
	assert(gaia_parse(BATTERY_REPLY, sizeof BATTERY_REPLY, &f) == 9);
	assert(f.cmd == 0x0703 && f.vendor == 0x0495 && f.payload_len == 1);
	assert(f.payload[0] == 100);

	assert(gaia_parse(MODE_REPLY, sizeof MODE_REPLY, &f) == 14);
	assert(f.cmd == 0x1B01 && f.payload_len == 6);

	/* truncation: every prefix must say "not yet", not crash */
	for (size_t i = 0; i < sizeof BATTERY_REPLY; i++) {
		assert(gaia_parse(BATTERY_REPLY, i, &f) == -1);
	}

	/* two frames in one buffer: consume one at a time */
	uint8_t two[64];
	memcpy(two, BATTERY_REPLY, sizeof BATTERY_REPLY);
	memcpy(two + 9, MODE_REPLY, sizeof MODE_REPLY);
	int used =
	    gaia_parse(two, sizeof BATTERY_REPLY + sizeof MODE_REPLY, &f);
	assert(used == 9 && f.cmd == 0x0703);
	used = gaia_parse(two + used, sizeof two - 9, &f);
	assert(used == 14 && f.cmd == 0x1B01);

	/* junk prefix resyncs */
	uint8_t junk[16] = {0x00, 0x11, 0x22};
	memcpy(junk + 3, BATTERY_REPLY, sizeof BATTERY_REPLY);
	assert(gaia_parse(junk, 3 + sizeof BATTERY_REPLY, &f) == 3 + 9);
	assert(f.cmd == 0x0703);

	/* bogus length must not read past the buffer */
	const uint8_t liar[] = {0xFF, 0x03, 0xFF, 0xFF, 0x04, 0x95, 0x07, 0x03};
	assert(gaia_parse(liar, sizeof liar, &f) == -1);

	/* decode: a mode reply updates transparency and adaptive at once */
	assert(gaia_parse(MODE_REPLY, sizeof MODE_REPLY, &f) == 14);
	gaia_field fields[2];
	int values[2];
	size_t n = gaia_decode(&f, fields, values, 2);
	assert(n == 2);
	assert(fields[0] == GAIA_FIELD_TRANSPARENCY && values[0] == 0);
	assert(fields[1] == GAIA_FIELD_ADAPTIVE && values[1] == 1);

	/* decode: acks and unknown frames carry no update */
	const uint8_t ack[] = {0xFF, 0x03, 0x00, 0x00, 0x04, 0x95, 0x1B, 0x00};
	assert(gaia_parse(ack, sizeof ack, &f) == 8);
	assert(gaia_decode(&f, fields, values, 2) == 0);

	/* decode: battery */
	assert(gaia_parse(BATTERY_REPLY, sizeof BATTERY_REPLY, &f) == 9);
	assert(gaia_decode(&f, fields, values, 2) == 1);
	assert(fields[0] == GAIA_FIELD_BATTERY && values[0] == 100);

	/* decode: firmware 3.38.3 as three u16s */
	const uint8_t fw[] = {
	    0xFF, 0x03, 0x00, 0x06, 0x04, 0x95, 0x13,
	    0x01, 0x00, 0x03, 0x00, 0x26, 0x00, 0x03,
	};
	assert(gaia_parse(fw, sizeof fw, &f) == 14);
	assert(gaia_decode(&f, fields, values, 2) == 1);
	assert(fields[0] == GAIA_FIELD_FIRMWARE && values[0] == 33803);

	/* decode: bass boost, and a short firmware reply is refused */
	const uint8_t bass[] = {0xFF, 0x03, 0x00, 0x01, 0x04,
				0x95, 0x11, 0x09, 1};
	assert(gaia_parse(bass, sizeof bass, &f) == 9);
	assert(gaia_decode(&f, fields, values, 2) == 1);
	assert(fields[0] == GAIA_FIELD_BASS && values[0] == 1);
	const uint8_t short_fw[] = {0xFF, 0x03, 0x00, 0x01, 0x04,
				    0x95, 0x13, 0x01, 0x03};
	assert(gaia_parse(short_fw, sizeof short_fw, &f) == 9);
	assert(gaia_decode(&f, fields, values, 2) == 0);

	/* equalizer: band set carries the band and a signed millibel gain */
	assert(gaia_set_field(GAIA_FIELD_EQ_BAND0, -30, out, sizeof out) == 10);
	assert(out[6] == 0x10 && out[7] == 0x01);
	assert(out[8] == 0x00 && out[9] == 0xE2); /* -3 dB */
	assert(gaia_get_field(GAIA_FIELD_EQ_BAND3, out, sizeof out) == 9);
	assert(out[6] == 0x10 && out[7] == 0x02 && out[8] == 0x03);

	/* equalizer: the reply has no band in it, so decode leaves it alone */
	const uint8_t band[] = {
	    0xFF, 0x03, 0x00, 0x01, 0x04, 0x95, 0x11, 0x02, 0xE2,
	};
	assert(gaia_parse(band, sizeof band, &f) == 9);
	assert(gaia_decode(&f, fields, values, 2) == 0);

	puts("gaia: ok");
	return 0;
}