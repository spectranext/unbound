#pragma once

#include <stdint.h>

extern void config_init();
uint8_t config_find_section();
uint8_t config_create_section();
uint8_t config_setCFString(const char* value) __z88dk_fastcall;
uint8_t config_getCFString(char* value) __z88dk_fastcall;
uint8_t config_commit_config();