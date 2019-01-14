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

#include <sof/gpio.h>
#include <sof/io.h>
#include <sof/iomux.h>

#include <platform/memory.h>
#include <platform/platform.h>

#define GPIO_OFFSET(x)		((x) + DW_GPIO_BASE)
#define GPIO_PORTA_DAT_REG	GPIO_OFFSET(0x00)
#define GPIO_PORTA_DIR_REG	GPIO_OFFSET(0x04)
#define GPIO_PORTA_CTL_REG	GPIO_OFFSET(0x08)

struct iomux;

struct gpio {
	uint32_t base;
	struct iomux *mux;
};

static struct gpio dw_gpio = {
	.base = DW_GPIO_BASE,
};

int gpio_write(const struct gpio *gpio, unsigned int port,
	       enum gpio_level level)
{
	io_reg_update_bits(GPIO_PORTA_DAT_REG, 1 << port,
			   (level == GPIO_LEVEL_HIGH) << port);

	return 0;
}

int gpio_read(const struct gpio *gpio, unsigned int port)
{
	return (io_reg_read(GPIO_PORTA_DAT_REG) >> port) & 1 ?
		GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

int gpio_configure(const struct gpio *gpio, unsigned int port,
		   const struct gpio_config *config)
{
	int ret = iomux_configure(gpio->mux, port, IOMUX_CONFIG_GPIO);
	if (ret < 0)
		return ret;

	/* set the direction of GPIO */
	io_reg_update_bits(GPIO_PORTA_DIR_REG, 1 << port,
			(config->direction == GPIO_DIRECTION_OUTPUT) << port);

	return 0;
}

const struct gpio *gpio_get(unsigned int id)
{
	if (id)
		return NULL;

	dw_gpio.mux = iomux_get(PLATFORM_IOMUX_GPIO0);

	return &dw_gpio;
}

int gpio_probe(const struct gpio *gpio)
{
	if (gpio != &dw_gpio)
		return -ENODEV;

	return dw_gpio.mux ? iomux_probe(dw_gpio.mux) : 0;
}
