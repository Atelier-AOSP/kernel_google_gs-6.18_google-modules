# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(src)/../../include
EXTRA_CFLAGS += -I$(srctree)
obj-$(CONFIG_PIXEL_METRICS) += pixel_metrics.o
pixel_metrics-y += init_metrics.o
pixel_metrics-$(CONFIG_PERF_METRICS) += perf_metrics.o
pixel_metrics-$(CONFIG_THERMAL_METRICS) += thermal_metrics.o
pixel_metrics-$(CONFIG_CPUIDLE_METRICS) += cpuidle_metrics.o
