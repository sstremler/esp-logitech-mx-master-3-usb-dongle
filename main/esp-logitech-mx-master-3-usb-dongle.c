#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "misc.h"
#include "peer.h"
#include "tinyusb.h"
#include <inttypes.h>

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

static ble_uuid_any_t hid_over_gatt_svc_uuid;
static ble_uuid_any_t hid_over_gatt_chr_uuid;
static ble_uuid_any_t battery_svc_uuid;
static ble_uuid_any_t battery_chr_uuid;

static const char *tag = "LOGITECH_DONGLE";

const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))};

const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},    // 0: is supported language is English (0x0409)
    "Logitech",              // 1: Manufacturer
    "MX Master 3",           // 2: Product
    "123456",                // 3: Serials, should use chip ID
    "Example HID interface", // 4: HID
};

static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

void ble_store_config_init(void);
static void scan(void);

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
}

static int on_characteristic_subscribe(uint16_t conn_handle,
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

static int on_characteristic_read(uint16_t conn_handle,
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
        ESP_LOGI(tag, "Read successful. Value:");

        for (int i = 0; i < attr->om->om_len; i++)
        {
            ESP_LOGI(tag, "0x%02X ", attr->om->om_data[i]);
        }
    }

    MODLOG_DFLT(INFO, "\n");

    return 0;
}

static void read_battery_status(const struct peer *peer)
{
    const struct peer_chr *chr;
    int rc;

    chr = peer_chr_find_uuid(peer,
                             (ble_uuid_t *)&battery_svc_uuid,
                             (ble_uuid_t *)&battery_chr_uuid);
    if (chr == NULL)
    {
        MODLOG_DFLT(ERROR, "Error: Peer doesn't have the custom subscribable characteristic\n");
        /* Terminate the connection */
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    else
    {
        ESP_LOGI(tag, "read handle: 0x%02X", chr->chr.val_handle);

        rc = ble_gattc_read(peer->conn_handle, chr->chr.val_handle, on_characteristic_read, NULL);

        if (rc != 0)
        {
            MODLOG_DFLT(ERROR,
                        "Error: Failed to read the custom subscribable characteristic; "
                        "rc=%d\n",
                        rc);
            /* Terminate the connection */
            ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }

    return;
}

static void subscribe_to_mouse_events(const struct peer *peer, uint16_t handle)
{
    const struct peer_dsc *dsc;
    int rc;
    uint8_t value[2];

    dsc = peer_dsc_find_uuid(peer,
                             (ble_uuid_t *)&hid_over_gatt_svc_uuid,
                             (ble_uuid_t *)&hid_over_gatt_chr_uuid,
                             BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));
    if (dsc == NULL)
    {
        MODLOG_DFLT(ERROR, "Error: Peer lacks a CCCD for the subscribable characteristic\n");
        /* Terminate the connection */
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    else
    {
        ESP_LOGI(tag, "subscribe to mouse events, handle: 0x%02X", handle);

        /*** Write 0x00 and 0x01 (The subscription code) to the CCCD ***/
        value[0] = 1;
        value[1] = 0;
        rc = ble_gattc_write_flat(peer->conn_handle, handle,
                                  value, sizeof(value), on_characteristic_subscribe, NULL);
        if (rc != 0)
        {
            MODLOG_DFLT(ERROR,
                        "Error: Failed to subscribe to the subscribable characteristic; "
                        "rc=%d\n",
                        rc);
            /* Terminate the connection */
            ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }

    return;
}

static void on_service_discovery_complete(const struct peer *peer, int status, void *arg)
{

    if (status != 0)
    {
        /* Service discovery failed. Terminate the connection. */
        MODLOG_DFLT(ERROR, "Error: Service discovery failed; status=%d "
                           "conn_handle=%d\n",
                    status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    MODLOG_DFLT(INFO, "Service discovery complete; status=%d "
                      "conn_handle=%d\n",
                status, peer->conn_handle);

    // Subscribe to thumb button events.
    subscribe_to_mouse_events(peer, 0x0030);
    vTaskDelay(pdMS_TO_TICKS(50));
    // Subscribe to scroll and click events.
    subscribe_to_mouse_events(peer, 0x0034);
    // read_battery_status(peer);
}

static int on_gap_event_receive(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        /* A new device was discovered. */
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        char s[BLE_HS_ADV_MAX_SZ];

        if (fields.name != NULL)
        {
            assert(fields.name_len < sizeof s - 1);
            memcpy(s, fields.name, fields.name_len);
            s[fields.name_len] = '\0';

            if (strcmp(s, "MX Master 3 Mac") == 0)
            {
                ESP_LOGI(tag, "Found mouse");
                print_bytes(event->disc.addr.val, 6);

                rc = ble_gap_disc_cancel();
                if (rc != 0)
                {
                    MODLOG_DFLT(DEBUG, "Failed to cancel scan; rc=%d\n", rc);
                    return 0;
                }
                else
                {
                    ESP_LOGI(tag, "Scan stopped");
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
                    ESP_LOGI(tag, "Connected to mouse");
                }
            }
        }

        return 0;

    case BLE_GAP_EVENT_LINK_ESTAB:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0)
        {
            MODLOG_DFLT(INFO, "Connection established ");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
            MODLOG_DFLT(INFO, "\n");

            /* Remember peer. */
            rc = peer_add(event->connect.conn_handle);
            if (rc != 0 && rc != 2)
            {
                MODLOG_DFLT(ERROR, "Failed to add peer; rc=%d\n", rc);
                return 0;
            } else if(rc == 2) {
                MODLOG_DFLT(INFO, "Peer is already added; rc=%d\n", rc);
            }

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
        }
        else
        {
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
        scan();

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

        /*** Go for service discovery after encryption has been successfully enabled ***/
        rc = peer_disc_all(event->connect.conn_handle, on_service_discovery_complete, NULL);
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

        if (event->notify_rx.attr_handle == 0x33)
        {
            if (tud_hid_ready())
            {
                int32_t val = (buf[4] << 16) | (buf[3] << 8) | buf[2];
                int32_t x = val >> 12;
                if (x & 0x800)
                {
                    x |= 0xFFFFF000; // Sign extend if negative
                }

                int32_t y = val & 0x00000FFF;
                if (y & 0x800)
                {
                    y |= 0xFFFFF000; // Sign extend if negative
                }

                tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, buf[0], y, x, buf[5], buf[6]);
            }
        }
        else if (event->notify_rx.attr_handle == 0x2F)
        {
            int modifier = buf[0];
            uint8_t keycode[6] = {0};

            memcpy(keycode, buf + 1, (len - 1) > 6 ? 6 : (len - 1));
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycode);
        }

        free(buf);

        return 0;

    case BLE_GAP_EVENT_MTU:
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */
        ESP_LOGI(tag, "Repeat pairing.");
        return 0;
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

static void on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static void on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    scan();
}

void host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    ESP_LOGI(tag, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor,
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(tag, "USB initialization DONE");

    ble_uuid_from_str(&battery_svc_uuid, "0000180f-0000-1000-8000-00805f9b34fb");
    ble_uuid_from_str(&battery_chr_uuid, "00002a19-0000-1000-8000-00805f9b34fb");

    ble_uuid_from_str(&hid_over_gatt_svc_uuid, "1812");
    ble_uuid_from_str(&hid_over_gatt_chr_uuid, "2a4d");

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
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Initialize data structures to track connected peers. */
    int rc = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("logitech-mx-master-3-usb-dongle");
    assert(rc == 0);

    ble_store_config_init();
    nimble_port_freertos_init(host_task);
}
