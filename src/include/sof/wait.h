/*
 * Copyright (c) 2016, Intel Corporation
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
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * Simple wait for event completion and signaling with timeouts.
 */

#ifndef __INCLUDE_WAIT__
#define __INCLUDE_WAIT__

#include <stdint.h>
#include <errno.h>
#include <arch/wait.h>
#include <sof/io.h>
#include <sof/debug.h>
#include <sof/work.h>
#include <sof/timer.h>
#include <sof/interrupt.h>
#include <sof/trace.h>
#include <sof/lock.h>
#include <sof/clk.h>
#include <platform/clk.h>
#include <platform/interrupt.h>
#include <sof/drivers/timer.h>
#include <platform/platform.h>

#if DEBUG_LOCKS
#define wait_atomic_check	\
	if (lock_dbg_atomic) { \
		trace_error_atomic(TRACE_CLASS_WAIT, "atm"); \
	}
#else
#define wait_atomic_check
#endif

#define DEFAULT_TRY_TIMES 8

typedef struct {
	uint32_t complete;
	struct work work;
	uint64_t timeout;
} completion_t;

void arch_wait_for_interrupt(int level);

static inline void wait_for_interrupt(int level)
{
	//tracev_event(TRACE_CLASS_WAIT, "WFE");
	wait_atomic_check;
	arch_wait_for_interrupt(level);
	//tracev_event(TRACE_CLASS_WAIT, "WFX");
}

static uint64_t _wait_cb(void *data, uint64_t delay)
{
	volatile completion_t *wc = (volatile completion_t*)data;

	wc->timeout = 1;
	return 0;
}

static inline uint32_t wait_is_completed(completion_t *comp)
{
	volatile completion_t *c = (volatile completion_t *)comp;

	return c->complete;
}

static inline void wait_completed(completion_t *comp)
{
	volatile completion_t *c = (volatile completion_t *)comp;

	c->complete = 1;
}

static inline void wait_init(completion_t *comp)
{
	volatile completion_t *c = (volatile completion_t *)comp;

	c->complete = 0;
	work_init(&comp->work, _wait_cb, comp, WORK_ASYNC);
}

static inline void wait_clear(completion_t *comp)
{
	volatile completion_t *c = (volatile completion_t *)comp;

	c->complete = 0;
}

/* simple interrupt based wait for completion */
static inline void wait_for_completion(completion_t *comp)
{
	/* check for completion after every wake from IRQ */
	while (comp->complete == 0)
		wait_for_interrupt(0);
}


/* simple interrupt based wait for completion with timeout */
static inline int wait_for_completion_timeout(completion_t *comp)
{
	volatile completion_t *c = (volatile completion_t *)comp;

	work_schedule_default(&comp->work, comp->timeout);
	comp->timeout = 0;

	/* check for completion after every wake from IRQ */
	while (1) {

		if (c->complete || c->timeout)
			break;

		wait_for_interrupt(0);
	}

	/* did we complete */
	if (c->complete) {
		/* no timeout so cancel work and return 0 */
		work_cancel_default(&comp->work);
		return 0;
	} else {
		/* timeout */
		trace_error_value(c->timeout);
		trace_error_value(c->complete);
		return -ETIME;
	}
}

/**
 * \brief Waits at least passed number of clocks.
 * \param[in] number_of_clks Minimum number of clocks to wait.
 */
static inline void wait_delay(uint64_t number_of_clks)
{
	uint64_t current = platform_timer_get(platform_timer);

	while ((platform_timer_get(platform_timer) - current) < number_of_clks)
		idelay(PLATFORM_DEFAULT_DELAY);
}

static inline int poll_for_completion_delay(completion_t *comp, uint64_t us)
{
	uint64_t tick = clock_ms_to_ticks(PLATFORM_DEFAULT_CLOCK, 1) *
						us / 1000;
	uint32_t tries = DEFAULT_TRY_TIMES;
	uint64_t delta = tick / tries;

	if (!delta) {
		delta = us;
		tries = 1;
	}

	while (!wait_is_completed(comp)) {
		if (!tries--) {
			trace_error(TRACE_CLASS_WAIT, "ewt");
			return -EIO;
		}

		wait_delay(delta);
	}

	return 0;
}

static inline int poll_for_register_delay(uint32_t reg,
					  uint32_t mask,
					  uint32_t val, uint64_t us)
{
	uint64_t tick = clock_ms_to_ticks(PLATFORM_DEFAULT_CLOCK, 1) *
						us / 1000;
	uint32_t tries = DEFAULT_TRY_TIMES;
	uint64_t delta = tick / tries;

	if (!delta) {
		delta = us;
		tries = 1;
	}

	while ((io_reg_read(reg) & mask) != val) {
		if (!tries--) {
			trace_error(TRACE_CLASS_WAIT, "ewt");
			return -EIO;
		}
		wait_delay(delta);
	}
	return 0;
}

#endif
