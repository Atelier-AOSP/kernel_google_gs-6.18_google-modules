/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Interface between the EL1 and EL2 halves of the pKVM S2MPU driver on
 * android16-6.12 (kvm_iommu_ops framework).
 *
 * The EL1 driver enumerates all S2MPUs from DT before the hypervisor is
 * finalized, fills an array of struct s2mpu_hyp_dev (page-aligned allocation)
 * and an MPT descriptor page, publishes them through the EL2 module globals
 * below and calls kvm_iommu_init_hyp(). The EL2 init op takes ownership of
 * all of it.
 */
#ifndef __S2MPU_HYP_IFACE_H__
#define __S2MPU_HYP_IFACE_H__

#include <kvm/iommu.h>

#define S2MPU_MAX_SYNCS			8

/* IOMMU id 0 is reserved: kvm_get_iommu_id_by_of() returns 0 for "unknown". */
#define S2MPU_FIRST_ID			1

struct s2mpu_hyp_dev {
	/* Must stay first: EL2 converts with container_of(). */
	struct kvm_hyp_iommu		iommu;
	phys_addr_t			pa;
	void __iomem			*va;		/* EL2 VA, set at EL2 */
	u32				flags;		/* S2MPU_HAS_SYNC, S2MPU_DENY_ALL */
	u32				version;	/* EL2 private */
	u32				context_cfg_valid_vid; /* EL2 private */
	u32				nr_syncs;
	phys_addr_t			sync_pa[S2MPU_MAX_SYNCS]; /* S2 part of SysMMU_SYNC */
	void __iomem			*sync_va[S2MPU_MAX_SYNCS];
};

#endif /* __S2MPU_HYP_IFACE_H__ */
