// probe.c — M0 spike: prove GAIA v3 reaches the headset over RFCOMM.
// Throwaway. Dies once src/gaia.c and src/bt.c exist.

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int main(void) {
	// channel 15 is GAIA on the M4 */
	struct sockaddr_rc addr = {.rc_family = AF_BLUETOOTH, .rc_channel = 15};
	str2ba("80:C3:BA:66:BE:5C", &addr.rc_bdaddr);

	int fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if (fd < 0 || connect(fd, (struct sockaddr*)&addr, sizeof addr) < 0) {
		perror("connect");
		return 1;
	}

	// FF 03 | len 0 | vendor 0495 \ cmd 0603 = get battery
	const uint8_t get_battery[] = {0xFF, 0x03, 0x00, 0x00,
				       0x04, 0x95, 0x06, 0x03};
	write(fd, get_battery, sizeof(get_battery));

	uint8_t buf[512];
	ssize_t n = read(fd, buf, sizeof(buf));
	for (ssize_t i = 0; i < n; i++) {
		printf("%02X ", buf[i]);
	}
	printf("\n");

	close(fd);
	return 0;
}