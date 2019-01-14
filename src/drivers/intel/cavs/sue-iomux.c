/*
 * Copyright (c) 2019, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Intel Corporation nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Guennadi Liakhovetski <guennadi.liakhovetski@linux.intel.com>
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <sof/io.h>
#include <sof/iomux.h>

#include <platform/memory.h>

#define SUE_IOMUX_OFFSET(x)	((x) + EXT_CTRL_BASE)
#define SUE_IOMUX_CTL0_REG	SUE_IOMUX_OFFSET(0x30)
#define SUE_IOMUX_CTL1_REG	SUE_IOMUX_OFFSET(0x34)

#define SUE_PIN_NUM 25

struct iomux {
	/* 0 -- not configured as GPIO; 1 -- configured as GPIO */
	uint8_t	pin_state[SUE_PIN_NUM];
};

static struct iomux sue_iomux;

/* Configure as GPIO */
#define SUE_IOMUX_MODE_GPIO	1

int iomux_configure(struct iomux *iomux, unsigned int port,
		    enum iomux_function fn)
{
	uint32_t shift;

	if (fn != IOMUX_CONFIG_GPIO)
		return -EINVAL;

	switch (port) {
	case 0 ... 7:
		shift = port << 1;
		io_reg_update_bits(SUE_IOMUX_CTL1_REG, 3 << shift,
				   SUE_IOMUX_MODE_GPIO << shift);
		break;
	case 8:
		io_reg_update_bits(SUE_IOMUX_CTL1_REG, 1 << 16,
				   SUE_IOMUX_MODE_GPIO << 16);
		break;
	case 9 ... 12:
		io_reg_update_bits(SUE_IOMUX_CTL0_REG, 1 << 11,
				   SUE_IOMUX_MODE_GPIO << 11);
		break;
	case 13:
		io_reg_update_bits(SUE_IOMUX_CTL0_REG, 1 << 0,
				   SUE_IOMUX_MODE_GPIO << 0);
		break;
	case 14:
		io_reg_update_bits(SUE_IOMUX_CTL0_REG, 1 << 1,
				   SUE_IOMUX_MODE_GPIO << 1);
		break;
	case 15 ... 18:
		io_reg_update_bits(SUE_IOMUX_CTL0_REG, 1 << 9,
				   SUE_IOMUX_MODE_GPIO << 9);
		break;
	case 19 ... 22:
		io_reg_update_bits(SUE_IOMUX_CTL0_REG, 1 << 10,
				   SUE_IOMUX_MODE_GPIO << 10);
		break;
	case 23:
	case 24:
		io_reg_update_bits(SUE_IOMUX_CTL0_REG, 1 << 16,
				   SUE_IOMUX_MODE_GPIO << 16);
		break;
	case 25:
		io_reg_update_bits(SUE_IOMUX_CTL0_REG, 1 << 26,
				   SUE_IOMUX_MODE_GPIO << 26);
		break;
	default:
		return -EINVAL;
	}

	iomux->pin_state[port] = 1;

	return 0;
}

struct iomux *iomux_get(unsigned int id)
{
	return id ? NULL : &sue_iomux;
}

int iomux_probe(struct iomux *iomux)
{
	return iomux == &sue_iomux ? 0 : -ENODEV;
}
