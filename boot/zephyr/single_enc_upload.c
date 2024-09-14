

#include <zephyr/kernel.h>
#include <flash_map_backend/flash_map_backend.h>
#include "../boot_serial/src/boot_serial_priv.h"

#include "bootutil/bootutil_log.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);















int release_image_to_slot(uint8_t app_slot, uint8_t storage_slot, uint32_t enc_image_lenght)
{
    int rc;
    const struct flash_area* storage_partition = NULL;
    const struct flash_area* app_partition = NULL;
    uint8_t buffer[512];
    size_t offset = 0;
    size_t write_times = 0;
    BOOT_LOG_INF("release_image_to_slot app_slot %d ,enc_image_slot %d size %d", app_slot, storage_slot, enc_image_lenght);
    /*open app_slot partition*/
    rc = flash_area_open(flash_area_id_from_direct_image(app_slot), &app_partition);
    if (rc) {
        rc = MGMT_ERR_EINVAL;
        goto out;
    }
    /*earse app_slot partition*/
    const size_t area_size = flash_area_get_size(app_partition);
    rc = flash_area_erase(app_partition, 0, area_size);
    if (rc) {
        rc = MGMT_ERR_ENOMEM;
        goto out;
    }
    /*open storage_slot partition*/
    rc = flash_area_open(flash_area_id_from_direct_image(storage_slot), &storage_partition);
    if (rc) {
        rc = MGMT_ERR_EINVAL;
        goto out;
    }
    //write_times=(enc_image_lenght%sizeof(buffer)==0)?(enc_image_lenght%sizeof(buffer)):((enc_image_lenght%sizeof(buffer))+1)
    write_times = (enc_image_lenght + sizeof(buffer) - 1) / sizeof(buffer);
    for (size_t i = 0;i < write_times;i++)
    {
        rc = flash_area_read(storage_partition, offset, buffer, sizeof(buffer));
        if (rc) {
            rc = MGMT_ERR_ENOTSUP;
            goto out;
        }
        rc=flash_area_write(app_partition,offset,buffer,sizeof(buffer));
         if (rc) {
            rc = MGMT_ERR_ENOTSUP;
            goto out;
        }
        offset += sizeof(buffer);
    }
    BOOT_LOG_INF("release_image_to_slot success ");
    return rc;

out:
    BOOT_LOG_ERR("release_image_to_slot error code %d", rc);
    return rc;
}



