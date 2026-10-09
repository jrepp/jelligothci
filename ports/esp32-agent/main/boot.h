#ifndef JELLI_AGENT_BOOT_H
#define JELLI_AGENT_BOOT_H

#include <stdbool.h>

/* Confirms a freshly selected agent image once the app is up, so the bootloader
 * stops treating it as on trial; until then a reset returns to the pet (ADR-013). */
void jelli_agent_boot_confirm(void);

/* Accepts only the pet (project "jelligotchi") in factory and arms the switch. */
bool jelli_agent_boot_pet_arm(void);

/* Call after each reply is queued. Once armed, drains USB output, selects factory,
 * and restarts; a rejected switch disarms and logs. */
void jelli_agent_boot_poll(void);

#endif
