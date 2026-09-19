// SPDX-License-Identifier: MIT
#pragma once

/* System tick on TIM21, which is a 16-bit timer on STM32L0. */
#define CH_CFG_ST_RESOLUTION 16
#define CH_CFG_ST_FREQUENCY 1000

#include_next <chconf.h>
