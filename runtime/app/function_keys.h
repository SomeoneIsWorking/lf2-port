/* Bounded synthetic pulses for the original game's Win32 function-key commands. */
#ifndef LF2_FUNCTION_KEYS_H
#define LF2_FUNCTION_KEYS_H

#include <stdint.h>

int function_key_request(uint32_t vk);
void function_keys_tick(void);

#endif
