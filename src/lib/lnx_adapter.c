/*
 * Copyright (c) 2015 2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Implementation of Linux device adapter interface for general operations.
 */

#include "device_adapter.h"
#include "device_fw.h"
#include "lnx_adapter.h"
#include "smbios_utilities.h"
#include "system_utilities.h"
#include "system.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdint.h>
#include <string/s_str.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <system/system.h>
#include <unistd.h>

#include <persistence/logging.h>
#include "device_utilities.h"
#include "nfit_utilities.h"

#define	NVM_IOCTL_TARGET_LEN	20
#define	DATA_FORMAT_REVISION	1
#define	MANUFACTURING_INFO_VALID_FLAG	0xFF

NVM_BOOL is_driver_module_installed()
{
	COMMON_LOG_ENTRY();
	NVM_BOOL is_installed = 0;

	struct ndctl_ctx *ctx;
	if (ndctl_new(&ctx) >= 0)
	{
		// If the NFIT driver is alive, we'll be able to get a bus
		struct ndctl_bus *bus = ndctl_bus_get_by_provider(ctx, "ACPI.NFIT");
		if (bus != NULL)
		{
			is_installed = 1;
		}

		ndctl_unref(ctx);
	}

	COMMON_LOG_EXIT_RETURN_I(is_installed);
	return is_installed;
}

/*
 * No way to detect compatibility - NDCTL makes no assumptions about
 * driver built with the kernel and returns nothing if requested
 * functionality is unsupported by the driver.
 */
NVM_BOOL is_supported_driver_available()
{
	COMMON_LOG_ENTRY();
	NVM_BOOL is_supported = is_driver_module_installed();

	COMMON_LOG_EXIT_RETURN_I(is_supported);
	return is_supported;
}

/*
 * Retrieve the vendor specific NVDIMM driver version.
 */
int get_vendor_driver_revision(NVM_VERSION version_str, const NVM_SIZE str_len)
{
	int rc = NVM_ERR_BADDRIVER;
	COMMON_LOG_ENTRY();

	if (version_str == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, version buffer in NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (str_len == 0)
	{
		COMMON_LOG_ERROR("Invalid parameter, version buffer length is 0");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (is_driver_module_installed())
	{
		s_strcpy(version_str, "0.0.0.0", NVM_VERSION_LEN);
		rc = NVM_SUCCESS;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

NVM_UINT32 get_capabilities_format(NVM_UINT8 channel, NVM_UINT8 imc, NVM_UINT8 ways,
		NVM_BOOL recommended)
{
	NVM_UINT32 format = 0;

	switch (channel)
	{
		case INTERLEAVE_SIZE_256B:
			format |= BITMAP_INTERLEAVE_SIZE_256B;
			break;
		case INTERLEAVE_SIZE_4KB:
			format |= BITMAP_INTERLEAVE_SIZE_4KB;
			break;
	}

	switch (imc)
	{
		case INTERLEAVE_SIZE_256B:
			format |= BITMAP_INTERLEAVE_SIZE_256B << 8;
			break;
		case INTERLEAVE_SIZE_4KB:
			format |= BITMAP_INTERLEAVE_SIZE_4KB << 8;
			break;
	}

	switch (ways)
	{
		case INTERLEAVE_WAYS_1:
			format |= BITMAP_INTERLEAVE_WAYS_1 << 16;
			break;
	}

	// Recommended
	if (recommended)
	{
		format |= (0x1 << 31);
	}
	return format;
}

int enable_dimm(NVM_NFIT_DEVICE_HANDLE device_handle)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;
	struct ndctl_ctx *ctx;

	if ((rc = ndctl_new(&ctx)) >= 0)
	{
		rc = NVM_ERR_BADDEVICE;
		struct ndctl_bus *bus;

		ndctl_bus_foreach(ctx, bus)
		{
			struct ndctl_dimm *dimm;
			ndctl_dimm_foreach(bus, dimm)
			{
				if (ndctl_dimm_get_handle(dimm) == device_handle.handle)
				{
					rc = linux_err_to_nvm_lib_err(ndctl_dimm_enable(dimm));
				}
			}
		}
	}
	else
	{
		rc = linux_err_to_nvm_lib_err(rc);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int enable_all_regions()
{
	COMMON_LOG_ENTRY();

	int rc = NVM_SUCCESS;
	struct ndctl_ctx *ctx;

	if ((rc = ndctl_new(&ctx)) >= 0)
	{
		struct ndctl_bus *bus;
		rc = NVM_SUCCESS;

		ndctl_bus_foreach(ctx, bus)
		{
			struct ndctl_region *region;
			ndctl_region_foreach(bus, region)
			{
				struct ndctl_dimm *dimm;
				NVM_BOOL all_dimms_in_the_region_are_enabled = 1;

				ndctl_dimm_foreach_in_region(region, dimm)
				{
					if (!ndctl_dimm_is_enabled(dimm))
					{
						all_dimms_in_the_region_are_enabled = 0;
					}
				}

				if (all_dimms_in_the_region_are_enabled)
				{
					//WA: do not return an error if ndctl_region_enable fails
					//This should be removed when kernel 4.15 is avail.
					linux_err_to_nvm_lib_err(ndctl_region_enable(region));
				}
			}
		}
	}
	else
	{
		rc = linux_err_to_nvm_lib_err(rc);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int reenumerate_namespaces(NVM_NFIT_DEVICE_HANDLE device_handle)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if ((rc = enable_dimm(device_handle)) == NVM_SUCCESS)
	{
		rc = enable_all_regions();

		if (rc != NVM_SUCCESS)
		{
			COMMON_LOG_ERROR("Could not enable the interleaved set");
		}
	}
	else
	{
		COMMON_LOG_ERROR("Could not enable the DIMM");
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Get the capabilities of the host platform
 */
int get_platform_capabilities(struct bios_capabilities *p_capabilities)
{
	return get_pcat(p_capabilities);
}

/*
 * Get the number of DIMMs in the system's memory topology
 */
int get_topology_count()
{
	return get_topology_count_from_nfit();
}

/*
 * Get the system's memory topology
 */
int get_topology(const NVM_UINT8 count, struct nvm_topology *p_dimm_topo)
{
	return get_topology_from_nfit(count, p_dimm_topo);
}

/*
 * Get the details of a specific dimm
 */
int get_dimm_details(NVM_NFIT_DEVICE_HANDLE device_handle, struct nvm_details *p_dimm_details)
{
	int rc = NVM_SUCCESS;

	struct ndctl_ctx *ctx;

	if (p_dimm_details == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, p_dimm_details is null");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if ((rc = ndctl_new(&ctx)) >= 0)
	{
		rc = NVM_ERR_BADDEVICE;

		struct ndctl_bus *bus;
		ndctl_bus_foreach(ctx, bus)
		{
			struct ndctl_dimm *dimm;
			ndctl_dimm_foreach(bus, dimm)
			{
				if (ndctl_dimm_get_handle(dimm) == device_handle.handle)
				{
					NVM_UINT16 dimm_smbios_handle = ndctl_dimm_get_phys_id(dimm);
					rc = get_dimm_details_for_physical_id(dimm_smbios_handle, p_dimm_details);
				}
			}
		}

		ndctl_unref(ctx);
	}
	else
	{
		rc = linux_err_to_nvm_lib_err(rc);
	}

	return rc;
}

int get_smbios_inventory_count()
{
	COMMON_LOG_ENTRY();
	int rc = 0;

	NVM_UINT8 *p_smbios_table = NULL;
	size_t smbios_table_size = 0;
	rc = get_smbios_table_alloc(&p_smbios_table, &smbios_table_size);
	if (rc == NVM_SUCCESS)
	{
		rc = smbios_get_populated_memory_device_count(p_smbios_table, smbios_table_size);
	}

	if (p_smbios_table)
	{
		free(p_smbios_table);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}


int get_smbios_inventory(const NVM_UINT8 count, struct nvm_details *p_smbios_inventory)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (p_smbios_inventory == NULL)
	{
		COMMON_LOG_ERROR("nvm_details pointer was NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else
	{
		memset(p_smbios_inventory, 0, sizeof (struct nvm_details) * count);

		NVM_UINT8 *p_smbios_table = NULL;
		size_t smbios_table_size = 0;
		rc = get_smbios_table_alloc(&p_smbios_table, &smbios_table_size);
		if (rc == NVM_SUCCESS)
		{
			rc = smbios_table_to_nvm_details_array(
					p_smbios_table, smbios_table_size, p_smbios_inventory, count);
		}

		if (p_smbios_table)
		{
			free(p_smbios_table);
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int get_driver_feature_flags(struct driver_feature_flags *features)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	struct ndctl_ctx *ctx;
	if (features == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter - 'pointer to features' parameter is null");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if ((rc = ndctl_new(&ctx)) >= 0)
	{
		memset(features, 0, sizeof (*features));
		unsigned char valid_config = 0;
		rc = NVM_ERR_DRIVERFAILED;

		// Linux driver exposes DSM commands on a per DIMM basis as stated by ACPI 6.0
		// Try and find a DIMM that we can manage, assume if we find a manageable DIMM that
		// all manageable DIMMS on the system share the same available DSM commands.
		struct ndctl_bus *p_bus;
		struct ndctl_dimm *p_dimm;
		ndctl_bus_foreach(ctx, p_bus)
		{
			ndctl_dimm_foreach(p_bus, p_dimm)
			{
				NVM_NFIT_DEVICE_HANDLE dimm_handle;
				dimm_handle.handle = ndctl_dimm_get_handle(p_dimm);

				if (is_device_manageable(dimm_handle))
				{
					valid_config = 1;
					rc = NVM_SUCCESS;
					break;
				}
			}

			if (valid_config)
			{
				break;
			}
		}

		/*
		 * TODO: As functionality is added to lnx_adapter.c or to ndctl
		 * these bits need to be updated
		 */
		if (valid_config && p_dimm)
		{
			features->get_platform_capabilities = 1;
			features->get_topology = 1;
			features->get_interleave = 1;
			features->get_dimm_detail = 1;
			features->get_namespaces = 1;
			features->get_namespace_detail = 1;
			features->get_address_scrub_data = ndctl_dimm_is_cmd_supported(p_dimm,
				DSM_VENDOR_SPECIFIC);
			features->get_platform_config_data = ndctl_dimm_is_cmd_supported(p_dimm,
				DSM_VENDOR_SPECIFIC);
			features->get_boot_status = ndctl_dimm_is_cmd_supported(p_dimm, DSM_VENDOR_SPECIFIC);
			features->get_power_data = 1;
			features->get_security_state = ndctl_dimm_is_cmd_supported(p_dimm, DSM_VENDOR_SPECIFIC);
			features->get_log_page = ndctl_dimm_is_cmd_supported(p_dimm, DSM_VENDOR_SPECIFIC);
			features->get_features = ndctl_dimm_is_cmd_supported(p_dimm, DSM_VENDOR_SPECIFIC);
			features->set_features = ndctl_dimm_is_cmd_supported(p_dimm, DSM_VENDOR_SPECIFIC);
			features->create_namespace = 1;
			features->rename_namespace = 1;
			features->grow_namespace = 0;
			features->shrink_namespace = 0;
			features->delete_namespace = 1;
			features->enable_namespace = 1;
			features->disable_namespace = 1;
			features->set_security_state = ndctl_dimm_is_cmd_supported(p_dimm, DSM_VENDOR_SPECIFIC);
			features->enable_logging = 0;
			features->run_diagnostic = 0;
			features->set_platform_config = ndctl_dimm_is_cmd_supported(p_dimm,
				DSM_VENDOR_SPECIFIC);
			features->passthrough = ndctl_dimm_is_cmd_supported(p_dimm, DSM_VENDOR_SPECIFIC);
			features->start_address_scrub = ndctl_dimm_is_cmd_supported(p_dimm,
				DSM_VENDOR_SPECIFIC);
			features->app_direct_mode = 1;
			features->storage_mode = 1;
		}

		ndctl_unref(ctx);
	}
	else
	{
		rc = linux_err_to_nvm_lib_err(rc);
	}
	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;

}

int get_supported_block_sizes(struct nvm_driver_capabilities *p_capabilities)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;
	int found = 0;
	struct ndctl_ctx *ctx;

	p_capabilities->num_block_sizes = 0;

	if ((rc = ndctl_new(&ctx)) >= 0)
	{
		struct ndctl_bus *bus;
		ndctl_bus_foreach(ctx, bus)
		{
			struct ndctl_region *region;
			ndctl_region_foreach(bus, region)
			{
				int nstype = ndctl_region_get_nstype(region);
				if (ndctl_region_is_enabled(region) &&
					(nstype == ND_DEVICE_NAMESPACE_BLK))
				{
					struct ndctl_namespace *namespace;
					ndctl_namespace_foreach(region, namespace)
					{
						p_capabilities->num_block_sizes =
								ndctl_namespace_get_num_sector_sizes(namespace);

						for (int i = 0; i < p_capabilities->num_block_sizes; i++)
						{
							p_capabilities->block_sizes[i] =
								ndctl_namespace_get_supported_sector_size(namespace, i);
						}
						found = 1;
						break;
					}
				}
				if (found)
				{
					break;
				}
			}
			if (found)
			{
				break;
			}
		}
		ndctl_unref(ctx);
	}
	else
	{
		rc = linux_err_to_nvm_lib_err(rc);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Return driver capabilities
 */
int get_driver_capabilities(struct nvm_driver_capabilities *p_capabilities)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if ((rc = get_driver_feature_flags(&p_capabilities->features)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Unable to retrieve driver feature flags");
	}
	else
	{
		p_capabilities->min_namespace_size = ndctl_min_namespace_size();

		/*
		 * TODO: DE4750 For the foreseeable future the available block sizes for BTT and for
		 * namespaces are going to be the same. This might not always be true though.
		 */
		if (get_supported_block_sizes(p_capabilities) != NVM_SUCCESS)
		{
			COMMON_LOG_ERROR("Unable to retrieve driver supported block sizes");
		}
		p_capabilities->namespace_memory_page_allocation_capable = 1;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * ***************************************************************************************
 * Helper functions
 * ***************************************************************************************
 */

/*
 * Get the device path name and attempt to open the path to the device
 */
int open_ioctl_target(int *p_target, const char *dev_name)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;
	char target[NVM_IOCTL_TARGET_LEN];
	snprintf(target, sizeof (target), "/dev/%s", dev_name);
	*p_target = open(target, O_RDWR);
	if (*p_target < 0)
	{
		switch (errno)
		{
			case EACCES:
				rc = NVM_ERR_INVALIDPERMISSIONS;
				break;
			default:
				rc = NVM_ERR_DRIVERFAILED;
				break;
		}
		COMMON_LOG_ERROR_F("Unable to open the " NVM_DIMM_NAME " device %s. Error: %s",
				target, strerror(errno));
	}
	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Helper function to translate block driver errors to NVM Lib errors.
 */
int linux_err_to_nvm_lib_err(int crbd_err)
{
	COMMON_LOG_ENTRY();
	int ret = NVM_SUCCESS;
	if (crbd_err < 0)
	{
		switch (crbd_err)
		{
			case -EACCES :
				ret = NVM_ERR_INVALIDPERMISSIONS;
				break;
			case -EBADF :
				ret = NVM_ERR_BADDEVICE;
				break;
			case -EBUSY :
				ret = NVM_ERR_DEVICEBUSY;
				break;
			case -EFAULT :
				ret = NVM_ERR_UNKNOWN;
				break;
			case -EINVAL :
				ret = NVM_ERR_DRIVERFAILED;
				break;
			case -ENODEV :
				ret = NVM_ERR_BADDEVICE;
				break;
			case -ENOMEM :
				ret = NVM_ERR_NOMEMORY;
				break;
			case -ENOSPC :
				ret = NVM_ERR_BADSIZE;
				break;
			case -ENOTTY :
				ret = NVM_ERR_UNKNOWN;
				break;
			case -EPERM :
				ret = NVM_ERR_BADSECURITYSTATE;
				break;
			default :
				ret = NVM_ERR_DRIVERFAILED;
		}
		COMMON_LOG_ERROR_F("Linux driver error converted to lib error = %d", ret);
	}
	COMMON_LOG_EXIT_RETURN_I(ret);
	return (ret);
}
/*
 * Send an ioctl request to a file descriptor target, translate result to nvm error.
 *
 * An ioctl call to our driver returns 0 on success, -1 if the driver itself
 * encounters a problem in processing a request.
 *
 * A positive return code from the pass through ioctl indicates a problem with a
 * command to an Intel DIMM Gen 1 FW mail box. In this case, the return code will be a
 * positive value that directly maps to FW error code.
 */
int send_ioctl_command(int fd, unsigned long request, void* parg)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	errno = 0; // Clear the process wide errno var.
	if (ioctl(fd, request, parg) < 0)
	{
		rc = linux_err_to_nvm_lib_err(errno);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int get_test_result_count(enum driver_diagnostic diagnostic)
{
	int rc = NVM_ERR_NOTSUPPORTED;
	return rc;
}

int run_test(enum driver_diagnostic diagnostic, const NVM_UINT32 count,
		struct health_event results[])
{
	int rc = NVM_ERR_NOTSUPPORTED;
	return rc;
}

int get_job_count()
{
	int rc = NVM_ERR_NOTSUPPORTED;
	return rc;
}
