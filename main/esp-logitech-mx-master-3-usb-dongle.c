#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
// #include "store/config/ble_store_config.h"
#include "misc.h"
#include "peer.h"

/*** The UUID of the service containing the subscribable characteristic ***/
static ble_uuid_any_t remote_svc_uuid;

/*** The UUID of the subscribable chatacteristic ***/
static ble_uuid_any_t remote_chr_uuid;

static ble_uuid_any_t battery_svc_uuid;

static ble_uuid_any_t battery_chr_uuid;

void ble_store_config_init(void);

static const char *tag = "USB_DONGLE";
// static const uint8_t mouse_address[6] = {0xEA, 0x24, 0xC6, 0xC4, 0xCB, 0xDA};

bool equals(uint8_t *arr1, uint8_t *arr2, uint8_t length)
{
    for (int i = 0; i < length; i++)
    {
        if (arr1[i] != arr2[i])
        {
            return false;
        }
    }

    return true;
}

/**
 * Application Callback. Called when the custom subscribable characteristic
 * is subscribed to.
 **/
static int
on_custom_subscribe(uint16_t conn_handle,
                    const struct ble_gatt_error *error,
                    struct ble_gatt_attr *attr,
                    void *arg)
{
    MODLOG_DFLT(INFO,
                "Subscribe to the custom subscribable characteristic complete; "
                "status=%d conn_handle=%d",
                error->status, conn_handle);

    if (error->status == 0)
    {
        MODLOG_DFLT(INFO, " attr_handle=%d value=", attr->handle);
        print_mbuf(attr->om);
    }

    return 0;
}

/**
 * Application Callback. Called when the custom subscribable chatacteristic
 * in the remote GATT server is read.
 * Expect to get the recently written data.
 **/
static int
on_custom_read(uint16_t conn_handle,
               const struct ble_gatt_error *error,
               struct ble_gatt_attr *attr,
               void *arg)
{
    MODLOG_DFLT(INFO,
                "Read complete for the subscribable characteristic; "
                "status=%d conn_handle=%d",
                error->status, conn_handle);
    if (error->status == 0)
    {
        MODLOG_DFLT(INFO, " attr_handle=%d value=", attr->handle);
        // print_mbuf(attr->om);
        ESP_LOGI("BLE", "Read successful. Value:");

        for (int i = 0; i < attr->om->om_len; i++)
        {
            ESP_LOGI(tag, "0x%02X ", attr->om->om_data[i]); // Print each byte in hex
        }
    }
    MODLOG_DFLT(INFO, "\n");

    return 0;
}

static void
read_battery_status(const struct peer *peer)
{
    const struct peer_chr *chr;
    int rc;
    // uint8_t value[2];

    chr = peer_chr_find_uuid(peer,
                             (ble_uuid_t *)&battery_svc_uuid,
                             (ble_uuid_t *)&battery_chr_uuid);
    if (chr == NULL)
    {
        MODLOG_DFLT(ERROR,
                    "Error: Peer doesn't have the custom subscribable characteristic\n");
        goto err;
    }

    ESP_LOGI(tag, "read handle: 0x%02X", chr->chr.val_handle);

    /*** Performs a read on the characteristic, the result is handled in blecent_on_new_read callback ***/
    rc = ble_gattc_read(peer->conn_handle, chr->chr.val_handle, on_custom_read, NULL);

    if (rc != 0)
    {
        MODLOG_DFLT(ERROR,
                    "Error: Failed to read the custom subscribable characteristic; "
                    "rc=%d\n",
                    rc);
        goto err;
    }

    return;
err:
    /* Terminate the connection */
    ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void
subscribe_to_mouse_events(const struct peer *peer, uint16_t handle)
{
    const struct peer_dsc *dsc;
    int rc;
    uint8_t value[2];

    dsc = peer_dsc_find_uuid(peer,
                             (ble_uuid_t *)&remote_svc_uuid,
                             (ble_uuid_t *)&remote_chr_uuid,
                             BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));
    if (dsc == NULL)
    {
        MODLOG_DFLT(ERROR, "Error: Peer lacks a CCCD for the subscribable characteristic\n");
        goto err;
    }

    ESP_LOGI(tag, "subscribe to mouse events, handle: 0x%02X", handle);

    /*** Write 0x00 and 0x01 (The subscription code) to the CCCD ***/
    value[0] = 1;
    value[1] = 0;
    rc = ble_gattc_write_flat(peer->conn_handle, handle,
                              value, sizeof(value), on_custom_subscribe, NULL);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR,
                    "Error: Failed to subscribe to the subscribable characteristic; "
                    "rc=%d\n",
                    rc);
        goto err;
    }

    return;
err:
    /* Terminate the connection */
    ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

/**
 * Called when service discovery of the specified peer has completed.
 */
static void
on_disc_complete(const struct peer *peer, int status, void *arg)
{

    if (status != 0)
    {
        /* Service discovery failed.  Terminate the connection. */
        MODLOG_DFLT(ERROR, "Error: Service discovery failed; status=%d "
                           "conn_handle=%d\n",
                    status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    /* Service discovery has completed successfully.  Now we have a complete
     * list of services, characteristics, and descriptors that the peer
     * supports.
     */
    MODLOG_DFLT(INFO, "Service discovery complete; status=%d "
                      "conn_handle=%d\n",
                status, peer->conn_handle);

    /* Now perform three GATT procedures against the peer: read,
     * write, and subscribe to notifications for the ANS service.
     */
    // subscribe to thumb button events
    subscribe_to_mouse_events(peer, 0x0030);
    // read_battery_status(peer);
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  blecent uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                                  blecent.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
on_gap_event_receive(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
#if MYNEWT_VAL(BLE_HCI_VS)
#if MYNEWT_VAL(BLE_POWER_CONTROL)
    struct ble_gap_set_auto_pcl_params params;
#endif
#endif
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        // ESP_LOGI(tag, "BLE_GAP_EVENT_DISC");
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data);

        // bool eq = equals(event->disc.addr.val, mouse_address, 6);

        char s[BLE_HS_ADV_MAX_SZ];

        if (fields.name != NULL)
        {
            assert(fields.name_len < sizeof s - 1);
            memcpy(s, fields.name, fields.name_len);
            s[fields.name_len] = '\0';
            // MODLOG_DFLT(DEBUG, "    name(%scomplete)=%s\n",
            // fields.name_is_complete ? "" : "in", s);

            if (strcmp(s, "MX Master 3 Mac") == 0)
            {
                ESP_LOGI(tag, "found mouse");
                print_bytes(event->disc.addr.val, 6);
                ESP_LOGI(tag, "device address type: %d", event->disc.addr.type);

                rc = ble_gap_disc_cancel();
                if (rc != 0)
                {
                    MODLOG_DFLT(DEBUG, "Failed to cancel scan; rc=%d\n", rc);
                    return 0;
                }
                else
                {
                    ESP_LOGI(tag, "scan stopped");
                }

                uint8_t own_addr_type;
                ble_addr_t *addr;

                /* Figure out address to use for connect (no privacy for now) */
                rc = ble_hs_id_infer_auto(0, &own_addr_type);
                if (rc != 0)
                {
                    MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
                    return 0;
                }
                else
                {
                    ESP_LOGI(tag, "address type found: %d", own_addr_type);
                }

                addr = &event->disc.addr;

                rc = ble_gap_connect(own_addr_type, addr, 30000, NULL,
                                     on_gap_event_receive, NULL);
                if (rc != 0)
                {
                    MODLOG_DFLT(ERROR, "Error: Failed to connect to device; addr_type=%d "
                                       "addr=%s; rc=%d\n",
                                addr->type, addr_str(addr->val), rc);
                    return 0;
                }
                else
                {
                    ESP_LOGI(tag, "connected");
                }
            }
        }

        // if (eq)
        // {
        //     ESP_LOGI(tag, "mouse found!");

        //     ESP_LOGI(tag, "device address type: %d", event->disc.addr.type);
        //     print_bytes(event->disc.addr.val, 6);
        // } else {
        //     ESP_LOGI(tag, "not mouse");
        // }

        // if (rc != 0)
        // {
        // return 0;
        // }
        // MODLOG_DFLT(DEBUG, "%s\n", "vmi");
        /* An advertisement report was received during GAP discovery. */
        // print_adv_fields(&fields);

        /* Try to connect to the advertiser if it looks interesting. */
        // blecent_connect_if_interesting(&event->disc);
        return 0;

    case BLE_GAP_EVENT_LINK_ESTAB:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0)
        {
            /* Connection successfully established. */
            MODLOG_DFLT(INFO, "Connection established ");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
            MODLOG_DFLT(INFO, "\n");

            /* Remember peer. */
            rc = peer_add(event->connect.conn_handle);
            if (rc != 0)
            {
                MODLOG_DFLT(ERROR, "Failed to add peer; rc=%d\n", rc);
                return 0;
            }

            // #if CONFIG_EXAMPLE_ENCRYPTION
            //             /** Initiate security - It will perform
            //              * Pairing (Exchange keys)
            //              * Bonding (Store keys)
            //              * Encryption (Enable encryption)
            //              * Will invoke event BLE_GAP_EVENT_ENC_CHANGE
            //              **/

            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0)
            {
                MODLOG_DFLT(INFO, "Security could not be initiated, rc = %d\n", rc);
                return ble_gap_terminate(event->connect.conn_handle,
                                         BLE_ERR_REM_USER_CONN_TERM);
            }
            else
            {
                MODLOG_DFLT(INFO, "Connection secured\n");
            }
            // #else
            /* Perform service discovery */
            // rc = peer_disc_all(event->connect.conn_handle,
            //             blecent_on_disc_complete, NULL);
            // if(rc != 0) {
            //     MODLOG_DFLT(ERROR, "Failed to discover services; rc=%d\n", rc);
            //     return 0;
            // }
        }
        else
        {
            /* Connection attempt failed; resume scanning. */
            MODLOG_DFLT(ERROR, "Error: Connection failed; status=%d\n",
                        event->connect.status);
        }

        return 0;

    case BLE_GAP_EVENT_CONNECT:
        MODLOG_DFLT(INFO, "connect; status=%d ", event->connect.status);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated. */
        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        MODLOG_DFLT(INFO, "discovery complete; reason=%d\n",
                    event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        MODLOG_DFLT(INFO, "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);

        ESP_LOGI(tag, "event->enc_change.conn_handle: %d", event->enc_change.conn_handle);
        ESP_LOGI(tag, "event->connect.conn_handle: %d", event->connect.conn_handle);

        /*** Go for service discovery after encryption has been successfully enabled ***/
        rc = peer_disc_all(event->connect.conn_handle, on_disc_complete, NULL);
        if (rc != 0)
        {
            MODLOG_DFLT(ERROR, "Failed to discover services; rc=%d\n", rc);
            return 0;
        }

        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        /* Peer sent us a notification or indication. */
        int len = OS_MBUF_PKTLEN(event->notify_rx.om);
        MODLOG_DFLT(INFO, "received %s; conn_handle=%d attr_handle=%d "
                          "attr_len=%d\n",
                    event->notify_rx.indication ? "indication" : "notification",
                    event->notify_rx.conn_handle,
                    event->notify_rx.attr_handle,
                    len);

                    uint8_t *buf = malloc(len + 1);
        os_mbuf_copydata(event->notify_rx.om, 0, len, buf);

        for (int i = 0; i < len; i++) {
            printf("%02X ", buf[i]);
        }
        printf("\n");

        if(event->notify_rx.attr_handle == 0x2F) {
            ESP_LOGI(tag, "thumb button pressed");
        }

        /* Attribute data is contained in event->notify_rx.om. Use
         * `os_mbuf_copydata` to copy the data received in notification mbuf */
        return 0;

    case BLE_GAP_EVENT_MTU:
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    // case BLE_GAP_EVENT_REPEAT_PAIRING:
    //     /* We already have a bond with the peer, but it is attempting to
    //      * establish a new secure link.  This app sacrifices security for
    //      * convenience: just throw away the old bond and accept the new link.
    //      */

    //     /* Delete the old bond. */
    //     rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
    //     assert(rc == 0);
    //     ble_store_util_delete_peer(&desc.peer_id_addr);

    //     /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
    //      * continue with the pairing operation.
    //      */
    //     return BLE_GAP_REPEAT_PAIRING_RETRY;
    default:
        return 0;
    }
}

static void scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 10;
    disc_params.window = 10;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      on_gap_event_receive, NULL);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "Error initiating GAP discovery procedure; rc=%d\n",
                    rc);
    }
}

static void blecent_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static void blecent_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

#if !CONFIG_EXAMPLE_INIT_DEINIT_LOOP
    /* Begin scanning for a peripheral to connect to. */
    scan();
#endif
}

void blecent_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

void app_main(void)
{
    ESP_LOGI(tag, "program started");

    ble_uuid_from_str(&battery_svc_uuid, "0000180f-0000-1000-8000-00805f9b34fb");
    ble_uuid_from_str(&battery_chr_uuid, "00002a19-0000-1000-8000-00805f9b34fb");

    ble_uuid_from_str(&remote_svc_uuid, "1812");
    ble_uuid_from_str(&remote_chr_uuid, "2a4d");

    int rc;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return;
    }

    /* Configure the host. */
    ble_hs_cfg.reset_cb = blecent_on_reset;
    ble_hs_cfg.sync_cb = blecent_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Initialize data structures to track connected peers. */
    rc = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("logitech-mx-master-3-usb-dongle");
    assert(rc == 0);

    /* XXX Need to have template for store */
    ble_store_config_init();

    nimble_port_freertos_init(blecent_host_task);

    // scan();

    ESP_LOGI(tag, "exit");
}
