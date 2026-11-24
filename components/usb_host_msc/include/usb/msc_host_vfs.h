/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_vfs_fat.h"
#include "usb/msc_host.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct msc_host_vfs *msc_host_vfs_handle_t;           /**< VFS handle to attached Mass Storage device */

/**
 * @brief Format an MSC device.
 *
 * @param[in] device Device handle obtained from the MSC initialization flow.
 * @param[in] mount_config FAT mount configuration used to derive the format parameters.
 * @param[in] vfs_handle Handle associated with the registered VFS instance.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if any argument is NULL
 *      - ESP_ERR_MSC_FORMAT_FAILED if formatting fails
 *      - ESP_ERR_NO_MEM if the formatter cannot allocate its work buffer
 */
esp_err_t msc_host_vfs_format(msc_host_device_handle_t device,
                              const esp_vfs_fat_mount_config_t *mount_config,
                              const msc_host_vfs_handle_t vfs_handle);

/**
 * @brief Filesystem type for msc_host_vfs_format_ex(). [Tab5 fork extension]
 */
typedef enum {
    MSC_HOST_VFS_FORMAT_FAT = 0,    /**< Format as FAT12/16/32 */
    MSC_HOST_VFS_FORMAT_EXFAT,      /**< Format as exFAT */
} msc_host_vfs_format_t;

/**
 * @brief Format an MSC device with an explicit filesystem type. [Tab5 fork extension]
 *
 * @param[in] device Device handle obtained from the MSC initialization flow.
 * @param[in] mount_config FAT mount configuration used to derive the format parameters.
 * @param[in] vfs_handle Handle associated with the registered VFS instance.
 * @param[in] format_type FAT or exFAT.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if any argument is NULL
 *      - ESP_ERR_MSC_FORMAT_FAILED if formatting fails
 *      - ESP_ERR_NO_MEM if the formatter cannot allocate its work buffer
 */
esp_err_t msc_host_vfs_format_ex(msc_host_device_handle_t device,
                                 const esp_vfs_fat_mount_config_t *mount_config,
                                 const msc_host_vfs_handle_t vfs_handle,
                                 msc_host_vfs_format_t format_type);

/**
 * @brief Register an MSC device with the VFS, tolerating an unmountable
 *        filesystem. [Tab5 fork extension]
 *
 * Unlike msc_host_vfs_register(), a mount failure does NOT tear the VFS and
 * diskio registration down: *vfs_handle stays valid so the caller can format
 * the drive with msc_host_vfs_format_ex() and register again.
 *
 * @param[in] device Device handle obtained from the MSC initialization flow.
 * @param[in] base_path Base VFS path used to access the file system.
 * @param[in] mount_config FAT mount configuration.
 * @param[out] vfs_handle Handle associated with the registered VFS instance.
 *        Valid on ESP_OK and on ESP_ERR_MSC_MOUNT_FAILED.
 *
 * @return
 *      - ESP_OK on success (filesystem mounted)
 *      - ESP_ERR_MSC_MOUNT_FAILED if the file system could not be mounted
 *        (the VFS remains registered; format and re-register to recover)
 *      - ESP_ERR_INVALID_ARG if any argument is NULL
 *      - ESP_ERR_NO_MEM if memory allocation fails
 */
esp_err_t msc_host_vfs_register_allow_unmounted(msc_host_device_handle_t device,
                                                const char *base_path,
                                                const esp_vfs_fat_mount_config_t *mount_config,
                                                msc_host_vfs_handle_t *vfs_handle);

/**
 * @brief Register an MSC device with the virtual file system.
 *
 * @param[in] device Device handle obtained from the MSC initialization flow.
 * @param[in] base_path Base VFS path used to access the file system.
 * @param[in] mount_config FAT mount configuration.
 * @param[out] vfs_handle Handle associated with the registered VFS instance.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if any argument is NULL
 *      - ESP_ERR_NO_MEM if memory allocation fails
 *      - ESP_ERR_MSC_MOUNT_FAILED if the file system could not be mounted
 *      - ESP_ERR_MSC_FORMAT_FAILED if formatting after a mount failure is requested and fails
 *      - Other error codes from the FAT VFS layer
 */
esp_err_t msc_host_vfs_register(msc_host_device_handle_t device,
                                const char *base_path,
                                const esp_vfs_fat_mount_config_t *mount_config,
                                msc_host_vfs_handle_t *vfs_handle);


/**
 * @brief Unregister an MSC device from the virtual file system.
 *
 * @param[in] vfs_handle VFS handle returned by msc_host_vfs_register().
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if vfs_handle is NULL
 */
esp_err_t msc_host_vfs_unregister(msc_host_vfs_handle_t vfs_handle);

#ifdef __cplusplus
}
#endif
