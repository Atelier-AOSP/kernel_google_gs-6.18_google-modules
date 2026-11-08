// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 *
 * android16-6.12 port: EL1 glue between the S2MPU platform driver and the
 * pKVM kvm_iommu framework.
 *
 * pKVM calls our kvm_iommu_driver::init_driver() from finalize_pkvm(), right
 * after the early pKVM modules (kvm-arm.protected_modules=...,pkvm_s2mpu) have
 * been loaded and before the host is deprivileged. The hypervisor must know
 * every S2MPU at that point, so they are enumerated from DT here instead of
 * being registered one by one from probe() like on 6.1.
 */

#include <linux/kvm_host.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <asm/kvm_host.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_pkvm_module.h>
#include <kvm/iommu.h>

#include "kvm_s2mpu.h"
#include "s2mpu-hyp-iface.h"
#include "s2mpu-kern.h"

extern struct s2mpu_hyp_dev *kvm_nvhe_sym(s2mpu_hyp_devs);
extern u32 kvm_nvhe_sym(s2mpu_hyp_nr_devs);
extern struct mpt *kvm_nvhe_sym(s2mpu_hyp_mpt_desc);
extern struct kvm_iommu_ops kvm_nvhe_sym(s2mpu_kvm_iommu_ops);
/* Declare EL2 module init function as it is needed by pkvm_load_el2_module. */
int kvm_nvhe_sym(s2mpu_hyp_init)(const struct pkvm_module_ops *ops);

static unsigned long token;

#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x)) *)(pkvm_el2_mod_va(&kvm_nvhe_sym(x), token)))

static const struct of_device_id s2mpu_hyp_of_match[] = {
	{ .compatible = "google," S2MPU_NAME },
	{},
};

static const struct of_device_id sysmmu_sync_hyp_of_match[] = {
	{ .compatible = "google,sysmmu_sync" },
	{},
};

/* Index i describes the S2MPU with IOMMU id S2MPU_FIRST_ID + i. */
static struct device_node **s2mpu_nodes;
static u32 s2mpu_nr_nodes;

/* The hypervisor owns the S2MPUs (MMIO donated, power domains registered). */
static bool s2mpu_hyp_ready;
/* EL2 init was attempted: MMIO may have been donated even if it failed. */
static bool s2mpu_hyp_touched;

bool pkvm_s2mpu_hyp_ready(void)
{
	return s2mpu_hyp_ready;
}

bool pkvm_s2mpu_mmio_owned_by_hyp(void)
{
	return s2mpu_hyp_touched;
}

static pkvm_handle_t s2mpu_id_by_of(struct device_node *np)
{
	u32 i;

	for (i = 0; i < s2mpu_nr_nodes; i++) {
		if (s2mpu_nodes[i] == np)
			return S2MPU_FIRST_ID + i;
	}
	return 0;
}

pkvm_handle_t pkvm_s2mpu_id(struct device *dev)
{
	return s2mpu_id_by_of(dev->of_node);
}

static int s2mpu_parse_syncs(struct device_node *np, struct s2mpu_hyp_dev *hdev)
{
	struct device_node *sync_np;
	struct resource res;
	int i, ret;

	for (i = 0; (sync_np = of_parse_phandle(np, "sysmmu_syncs", i)); i++) {
		if (!of_match_node(sysmmu_sync_hyp_of_match, sync_np)) {
			pr_err("%pOF: %pOF is not sysmmu_sync compatible\n", np, sync_np);
			of_node_put(sync_np);
			return -EINVAL;
		}

		ret = of_address_to_resource(sync_np, 0, &res);
		of_node_put(sync_np);
		if (ret)
			return ret;

		if (resource_size(&res) != SYSMMU_SYNC_MMIO_SIZE ||
		    hdev->nr_syncs >= S2MPU_MAX_SYNCS)
			return -EINVAL;

		hdev->sync_pa[hdev->nr_syncs++] = res.start + SYSMMU_SYNC_S2_OFFSET;
	}
	return 0;
}

static int s2mpu_enumerate(struct s2mpu_hyp_dev **out_devs, u32 *out_nr)
{
	struct s2mpu_hyp_dev *hdevs;
	struct device_node *np;
	struct resource res;
	u32 nr = 0, i = 0;
	int ret;

	for_each_matching_node(np, s2mpu_hyp_of_match)
		if (of_device_is_available(np))
			nr++;

	if (!nr)
		return -ENODEV;

	s2mpu_nodes = kcalloc(nr, sizeof(*s2mpu_nodes), GFP_KERNEL);
	hdevs = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					 get_order(nr * sizeof(*hdevs)));
	if (!s2mpu_nodes || !hdevs) {
		ret = -ENOMEM;
		goto err;
	}

	for_each_matching_node(np, s2mpu_hyp_of_match) {
		struct s2mpu_hyp_dev *hdev;

		if (!of_device_is_available(np))
			continue;
		if (WARN_ON(i >= nr)) {
			of_node_put(np);
			break;
		}

		hdev = &hdevs[i];

		ret = of_address_to_resource(np, 0, &res);
		if (ret || resource_size(&res) != S2MPU_MMIO_SIZE) {
			pr_err("%pOF: bad 'reg'\n", np);
			of_node_put(np);
			ret = -EINVAL;
			goto err;
		}
		hdev->pa = res.start;

		if (of_property_read_bool(np, "built-in-sync"))
			hdev->flags |= S2MPU_HAS_SYNC;
		if (of_property_read_bool(np, "deny-all"))
			hdev->flags |= S2MPU_DENY_ALL;

		ret = s2mpu_parse_syncs(np, hdev);
		if (ret) {
			of_node_put(np);
			goto err;
		}

		hdev->iommu.power_domain.type = KVM_POWER_DOMAIN_HOST_HVC;
		hdev->iommu.power_domain.device_id = S2MPU_FIRST_ID + i;
		hdev->iommu.power_is_off = true;

		s2mpu_nodes[i++] = of_node_get(np);
	}

	s2mpu_nr_nodes = i;
	*out_devs = hdevs;
	*out_nr = i;
	return 0;

err:
	if (s2mpu_nodes) {
		while (i--)
			of_node_put(s2mpu_nodes[i]);
		kfree(s2mpu_nodes);
		s2mpu_nodes = NULL;
	}
	if (hdevs)
		free_pages((unsigned long)hdevs, get_order(nr * sizeof(*hdevs)));
	return ret;
}

static struct mpt *s2mpu_alloc_mpt(void)
{
	const int smpt_order = SMPT_ORDER(MPT_PROT_BITS);
	struct mpt *mpt;
	unsigned int gb;

	/* Must fit MPT descriptor in a page, the hypervisor takes exactly one. */
	BUILD_BUG_ON(sizeof(*mpt) > PAGE_SIZE);
	mpt = (struct mpt *)get_zeroed_page(GFP_KERNEL);
	if (!mpt)
		return NULL;

	/* Buddy allocations are naturally aligned, as the S2MPU requires. */
	for_each_gb(gb) {
		mpt->fmpt[gb].smpt = (u32 *)__get_free_pages(GFP_KERNEL, smpt_order);
		if (!mpt->fmpt[gb].smpt)
			goto err;
	}
	return mpt;

err:
	for_each_gb(gb)
		if (mpt->fmpt[gb].smpt)
			free_pages((unsigned long)mpt->fmpt[gb].smpt, smpt_order);
	free_page((unsigned long)mpt);
	return NULL;
}

static int s2mpu_init_driver(void)
{
	struct kvm_hyp_memcache atomic_mc;
	struct s2mpu_hyp_dev *hdevs;
	struct mpt *mpt;
	u32 nr;
	int ret;

	ret = s2mpu_enumerate(&hdevs, &nr);
	if (ret) {
		pr_err("pkvm-s2mpu: DT enumeration failed: %d\n", ret);
		goto out_register;
	}

	mpt = s2mpu_alloc_mpt();
	if (!mpt) {
		ret = -ENOMEM;
		goto out_register;
	}

	/*
	 * Publish before loading the EL2 module: its sections are mapped into
	 * the hypervisor by pkvm_load_el2_module(). The memory pointed to is
	 * donated to EL2 by s2mpu_init() and never returned.
	 */
	kvm_nvhe_sym(s2mpu_hyp_devs) = hdevs;
	kvm_nvhe_sym(s2mpu_hyp_nr_devs) = nr;
	kvm_nvhe_sym(s2mpu_hyp_mpt_desc) = mpt;

	ret = pkvm_load_el2_module(kvm_nvhe_sym(s2mpu_hyp_init), &token);
	if (ret) {
		pr_err("pkvm-s2mpu: failed to load EL2 module: %d\n", ret);
		goto out_register;
	}

	s2mpu_hyp_touched = true;
	init_hyp_memcache(&atomic_mc);
	ret = kvm_iommu_init_hyp(ksym_ref_addr_nvhe(s2mpu_kvm_iommu_ops), &atomic_mc);
	if (ret) {
		pr_err("pkvm-s2mpu: EL2 init failed: %d\n", ret);
		goto out_register;
	}

	/* Snapshot the host stage-2 into the MPT; from now on it is kept in sync. */
	ret = kvm_iommu_alloc_domain(KVM_IOMMU_DOMAIN_IDMAP_ID, KVM_IOMMU_DOMAIN_IDMAP_TYPE);
	if (ret) {
		pr_err("pkvm-s2mpu: identity domain allocation failed: %d\n", ret);
		goto out_register;
	}

	s2mpu_hyp_ready = true;
	pr_info("pkvm-s2mpu: %u S2MPUs handed over to the hypervisor\n", nr);

out_register:
	/*
	 * Register the platform driver even on failure: GSA, AoC and others
	 * wait for the S2MPU devices to probe. Without pKVM ownership the
	 * driver falls back to kernel control where that is still possible.
	 */
	WARN_ON(pkvm_s2mpu_platform_register());
	return ret;
}

static void s2mpu_remove_driver(void)
{
}

static struct kvm_iommu_driver s2mpu_kvm_driver = {
	.init_driver		= s2mpu_init_driver,
	.remove_driver		= s2mpu_remove_driver,
	.get_iommu_id_by_of	= s2mpu_id_by_of,
};

/*
 * Returns 1 if the platform driver will be registered by init_driver(), 0 if
 * the caller must register it now (pKVM off or module loaded too late).
 */
int pkvm_s2mpu_kvm_register(void)
{
	int ret;

	if (!is_protected_kvm_enabled())
		return 0;

	/*
	 * Early pKVM modules are loaded from finalize_pkvm(), during initcalls.
	 * Anything later would never see init_driver().
	 */
	if (system_state >= SYSTEM_RUNNING) {
		pr_err("pkvm-s2mpu: loaded after pKVM finalization, add it to kvm-arm.protected_modules\n");
		return 0;
	}

	ret = kvm_iommu_register_driver(&s2mpu_kvm_driver);
	if (ret) {
		pr_err("pkvm-s2mpu: kvm_iommu_register_driver failed: %d\n", ret);
		return ret;
	}
	return 1;
}
