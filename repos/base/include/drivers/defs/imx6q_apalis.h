/*
 * \brief  MMIO and IRQ definitions of the i.MX6Quad Apalis
 * \author Stefan Kalkowski
 * \author Pirmin Duss
 * \date   2019-01-25
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 * Copyright (C) 2019 gapfruit AG
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__DRIVERS__DEFS__IMX6Q_APALIS_H_
#define _INCLUDE__DRIVERS__DEFS__IMX6Q_APALIS_H_

/* Genode includes */
#include <drivers/defs/imx6.h>

namespace Imx6q_apalis {

	using namespace Imx6;

	enum {
		/* normal RAM */
		RAM_BASE = 0x10000000,
		RAM_SIZE = 0x80000000,

		/* device IO memory 
		MMIO_BASE = 0x00000000,
		MMIO_SIZE = 0x10000000,

		UART_1_IRQ       = 58,
		UART_1_MMIO_BASE = 0x02020000,
		UART_1_MMIO_SIZE = 0x00004000,

		timer 
		EPIT_2_IRQ       = 89,
		EPIT_2_MMIO_BASE = 0x020d4000,
		EPIT_2_MMIO_SIZE = 0x00004000,

		ARM IP Bus control 
		AIPS_1_MMIO_BASE = 0x0207c000,
		AIPS_1_MMIO_SIZE = 0x00004000,
		AIPS_2_MMIO_BASE = 0x0217c000,
		AIPS_2_MMIO_SIZE = 0x00004000,

		CPU 
		CORTEX_A9_PRIVATE_MEM_BASE  = 0x00a00000,
		CORTEX_A9_PRIVATE_MEM_SIZE  = 0x00002000,
		CORTEX_A9_PRIVATE_TIMER_CLK = 500000000,  timer clk runs half the CPU freq 
		CORTEX_A9_PRIVATE_TIMER_DIV = 100,

		L2 cache controller 
		PL310_MMIO_BASE = 0x00a02000,
		PL310_MMIO_SIZE = 0x00001000,

		System reset controller 
		SRC_MMIO_BASE = 0x20d8000,

		CPU cache
		CACHE_LINE_SIZE_LOG2 = 5,
		*/
	};
};

#endif /* _INCLUDE__DRIVERS__DEFS__IMX6Q_APALIS_H_ */
