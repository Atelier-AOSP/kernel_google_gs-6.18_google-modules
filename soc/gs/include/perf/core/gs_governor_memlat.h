/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * gs201 shim for the vendor scheduler (vh/kernel/sched/cpufreq_gov.c).
 *
 * Laguna and later SoCs build the perf/core memory latency governor, which the
 * schedutil fast-switch path notifies on every frequency change. gs201 uses
 * soc/gs/drivers/performance/lat_governors instead (no fast-switch hook), and
 * Google's gs201 build never loads perf/core/lat_govs, so this is a no-op.
 * The call is also gated by the memlat_fast_switch_enable static key, which
 * is off unless enabled through /proc/vendor_sched.
 */
#ifndef _GS_GOVERNOR_MEMLAT_H_
#define _GS_GOVERNOR_MEMLAT_H_

struct cpufreq_policy;

static inline void gs_governor_memlat_cpufreq_update(struct cpufreq_policy *policy,
						     unsigned int target_freq)
{
}

#endif /* _GS_GOVERNOR_MEMLAT_H_ */
