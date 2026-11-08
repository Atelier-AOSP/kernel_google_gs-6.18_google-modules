/* SPDX-License-Identifier: GPL-2.0-only */
/* EL1-internal interface of the pKVM S2MPU driver (android16-6.12 port). */
#ifndef __S2MPU_KERN_H__
#define __S2MPU_KERN_H__

#include <linux/device.h>
#include <linux/kvm_host.h>

/* s2mpu.c */
int pkvm_s2mpu_kvm_register(void);
bool pkvm_s2mpu_hyp_ready(void);
bool pkvm_s2mpu_mmio_owned_by_hyp(void);
pkvm_handle_t pkvm_s2mpu_id(struct device *dev);

/* pkvm_s2mpu.c */
int pkvm_s2mpu_platform_register(void);

#endif /* __S2MPU_KERN_H__ */
