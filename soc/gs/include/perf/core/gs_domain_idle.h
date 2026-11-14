/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * gs201 shim for the vendor scheduler (vh/kernel/sched/init.c).
 *
 * On Laguna and later, register_set_cluster_enabled_cb() comes from the
 * perf/core gs_domain_idle module. On gs201 it is exported by cal-if
 * (soc/gs/drivers/soc/google/cal-if/cal-if.c), so only declare it here.
 */
#ifndef _GS_DOMAIN_IDLE_H_
#define _GS_DOMAIN_IDLE_H_

void register_set_cluster_enabled_cb(void (*func)(int, int));

#endif /* _GS_DOMAIN_IDLE_H_ */
