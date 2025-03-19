/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <assert.h>
#include <string.h>
#include "host/ble_hs.h"
#include "esp_central.h"

struct peer *
peer_find(uint16_t conn_handle);

void
peer_disc_complete(struct peer *peer, int rc);

struct peer_dsc *
peer_dsc_find_prev(const struct peer_chr *chr, uint16_t dsc_handle);

struct peer_dsc *
peer_dsc_find(const struct peer_chr *chr, uint16_t dsc_handle,
              struct peer_dsc **out_prev);

int
peer_dsc_add(struct peer *peer, uint16_t chr_val_handle,
             const struct ble_gatt_dsc *gatt_dsc);

void
peer_disc_dscs(struct peer *peer);

int
peer_dsc_disced(uint16_t conn_handle, const struct ble_gatt_error *error,
                uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                void *arg);

uint16_t
chr_end_handle(const struct peer_svc *svc, const struct peer_chr *chr);

int chr_is_empty(const struct peer_svc *svc, const struct peer_chr *chr);

struct peer_chr *
peer_chr_find_prev(const struct peer_svc *svc, uint16_t chr_val_handle);

struct peer_chr *
peer_chr_find(const struct peer_svc *svc, uint16_t chr_val_handle,
              struct peer_chr **out_prev);

void
peer_chr_delete(struct peer_chr *chr);

int
peer_chr_add(struct peer *peer, uint16_t svc_start_handle,
             const struct ble_gatt_chr *gatt_chr);

int
peer_chr_disced(uint16_t conn_handle, const struct ble_gatt_error *error,
                const struct ble_gatt_chr *chr, void *arg);

void
peer_disc_chrs(struct peer *peer);

int peer_svc_is_empty(const struct peer_svc *svc);

struct peer_svc *
peer_svc_find_prev(struct peer *peer, uint16_t svc_start_handle);

struct peer_svc *
peer_svc_find(struct peer *peer, uint16_t svc_start_handle,
              struct peer_svc **out_prev);

struct peer_svc *
peer_svc_find_range(struct peer *peer, uint16_t attr_handle);

const struct peer_svc *
peer_svc_find_uuid(const struct peer *peer, const ble_uuid_t *uuid);

const struct peer_chr *
peer_chr_find_uuid(const struct peer *peer, const ble_uuid_t *svc_uuid,
                   const ble_uuid_t *chr_uuid);

const struct peer_dsc *
peer_dsc_find_uuid(const struct peer *peer, const ble_uuid_t *svc_uuid,
                   const ble_uuid_t *chr_uuid, const ble_uuid_t *dsc_uuid);

int
peer_svc_add(struct peer *peer, const struct ble_gatt_svc *gatt_svc);

void
peer_svc_delete(struct peer_svc *svc);

int
peer_svc_disced(uint16_t conn_handle, const struct ble_gatt_error *error,
                const struct ble_gatt_svc *service, void *arg);

int peer_disc_svc_by_uuid(uint16_t conn_handle, const ble_uuid_t *uuid, peer_disc_fn *disc_cb,
                          void *disc_cb_arg);

int peer_disc_all(uint16_t conn_handle, peer_disc_fn *disc_cb, void *disc_cb_arg);

int peer_delete(uint16_t conn_handle);

int peer_add(uint16_t conn_handle);

void peer_traverse_all(peer_traverse_fn *trav_cb, void *arg);

void
peer_free_mem(void);

int peer_init(int max_peers, int max_svcs, int max_chrs, int max_dscs);
