#ifndef BT_H
#define BT_H

#include <stddef.h>

/* Momentum 4 serves GAIA on this RFCOMM channel. */
#define BT_GAIA_CHANNEL 15

/* Opens the GAIA channel on mac. Returns fd, or -1 with *error set (owned). */
int bt_open(const char* mac, char** error);

/* First paired device that looks like a Momentum. 0 on success. */
int bt_find_device(char* out, size_t len);

#endif // BT_H
