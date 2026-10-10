/*
 * Copyright (c) 2016, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <sound/apr_audio-v2.h>
#include <sound/adsp_err.h>

/*
 * The table is indexed by the ADSP_E* status an APR_BASIC_RSP_RESULT carries
 * in its second payload word. ADSP_ENOTIMPL and ADSP_EUNSUPPORTED both map to
 * -ENOSYS so that a caller can tell "the firmware lacks this operation" from
 * a retryable condition.
 */
struct adsp_err_code {
	int lnx_err_code;
	const char *adsp_err_str;
};

static const struct adsp_err_code adsp_err_code_info[ADSP_ERR_MAX + 1] = {
	[ADSP_EOK] = { 0, "ADSP_EOK" },
	[ADSP_EFAILED] = { -ENOTRECOVERABLE, "ADSP_EFAILED" },
	[ADSP_EBADPARAM] = { -EINVAL, "ADSP_EBADPARAM" },
	[ADSP_EUNSUPPORTED] = { -ENOSYS, "ADSP_EUNSUPPORTED" },
	[ADSP_EVERSION] = { -ENOPROTOOPT, "ADSP_EVERSION" },
	[ADSP_EUNEXPECTED] = { -ENOTRECOVERABLE, "ADSP_EUNEXPECTED" },
	[ADSP_EPANIC] = { -ENOTRECOVERABLE, "ADSP_EPANIC" },
	[ADSP_ENORESOURCE] = { -ENOSPC, "ADSP_ENORESOURCE" },
	[ADSP_EHANDLE] = { -EBADR, "ADSP_EHANDLE" },
	[ADSP_EALREADY] = { -EALREADY, "ADSP_EALREADY" },
	[ADSP_ENOTREADY] = { -EPERM, "ADSP_ENOTREADY" },
	[ADSP_EPENDING] = { -EINPROGRESS, "ADSP_EPENDING" },
	[ADSP_EBUSY] = { -EBUSY, "ADSP_EBUSY" },
	[ADSP_EABORTED] = { -ECANCELED, "ADSP_EABORTED" },
	[ADSP_EPREEMPTED] = { -EAGAIN, "ADSP_EPREEMPTED" },
	[ADSP_ECONTINUE] = { -EAGAIN, "ADSP_ECONTINUE" },
	[ADSP_EIMMEDIATE] = { -EAGAIN, "ADSP_EIMMEDIATE" },
	[ADSP_ENOTIMPL] = { -ENOSYS, "ADSP_ENOTIMPL" },
	[ADSP_ENEEDMORE] = { -ENODATA, "ADSP_ENEEDMORE" },
	[0x13] = { -EADV, "ADSP_E_UNDEFINED" },
	[ADSP_ENOMEMORY] = { -ENOMEM, "ADSP_ENOMEMORY" },
	[ADSP_ENOTEXIST] = { -ENODEV, "ADSP_ENOTEXIST" },
	[ADSP_ERR_MAX] = { -EADV, "ADSP_ERR_MAX" },
};

int adsp_err_get_lnx_err_code(u32 adsp_error)
{
	if (adsp_error > ADSP_ERR_MAX)
		adsp_error = ADSP_ERR_MAX;
	return adsp_err_code_info[adsp_error].lnx_err_code;
}

const char *adsp_err_get_err_str(u32 adsp_error)
{
	if (adsp_error > ADSP_ERR_MAX)
		adsp_error = ADSP_ERR_MAX;
	return adsp_err_code_info[adsp_error].adsp_err_str;
}
