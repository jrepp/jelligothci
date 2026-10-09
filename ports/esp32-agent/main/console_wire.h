#ifndef JELLI_AGENT_CONSOLE_WIRE_H
#define JELLI_AGENT_CONSOLE_WIRE_H

#include <stdbool.h>

/* Installs the USB serial driver, which logs share. Up to 512 bytes that arrive
 * before the console runs wait in its receive buffer; the driver drops more. */
bool jelli_agent_console_install(void);

/* Starts the console task, which answers one request at a time. Call after the
 * start-up confirmation (boot.h), so "boot pet" can never race it. */
bool jelli_agent_console_run(void);

#endif
