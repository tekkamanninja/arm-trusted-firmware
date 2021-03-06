/*
 * Copyright (c) 2013-2015, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Contains APU specific macros and macros to be defined depending on
 * the execution environment.
 */

#ifndef _PM_CLIENT_H_
#define _PM_CLIENT_H_

#include "pm_defs.h"
#include "pm_common.h"

/* Functions to be implemented by each PU */
void pm_client_suspend(const struct pm_proc *proc, unsigned int state);
void pm_client_abort_suspend(void);
void pm_client_wakeup(const struct pm_proc *proc);
enum pm_ret_status set_ocm_retention(void);

/* Global variables to be set in pm_client.c */
extern const struct pm_proc *primary_proc;

#endif /* _PM_CLIENT_H_ */
