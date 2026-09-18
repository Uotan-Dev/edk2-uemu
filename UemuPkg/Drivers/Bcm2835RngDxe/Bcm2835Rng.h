/** @file
 *
 *  Register definitions for the BCM2835 Random Number Generator.
 *  Adapted for uemu-ng.
 *
 *  Copyright (c) 2019, Pete Batard <pete@akeo.ie>.
 *  Copyright (c) 2026 Nuo Shen, Nanjing University
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef BCM2835_RNG_H__
#define BCM2835_RNG_H__

#define RNG_CTRL                            0x0
#define RNG_STATUS                          0x4
#define RNG_DATA                            0x8

#define RNG_CTRL_ENABLE                     0x1

#endif /* BCM2835_RNG_H__ */
