/* SPDX-License-Identifier: MIT */
#ifndef BT_LEGACY_ADV_H
#define BT_LEGACY_ADV_H

#include <stddef.h>
#include <stdint.h>

#define BT_LEGACY_ADV_COMMAND_MAX 35
#define BT_LEGACY_ADV_EVENT_MAX 8

struct bt_legacy_adv {
    uint8_t configured;
    uint8_t handle;
    uint8_t type;
    uint8_t enabled;
    uint8_t data_ready;
    uint8_t scan_response_ready;
    uint8_t scan_response_present;
    uint8_t tx_power;
};

struct bt_legacy_adv_request {
    uint16_t opcode;
    uint8_t command[BT_LEGACY_ADV_COMMAND_MAX];
    uint8_t length;
    uint8_t status;
    uint8_t response[2];
    uint8_t response_length;
    uint8_t handle;
};

int bt_legacy_adv_usb_match(uint16_t vendor, uint16_t product);
void bt_legacy_adv_reset(struct bt_legacy_adv *state);
int bt_legacy_adv_prepare(const struct bt_legacy_adv *state, const uint8_t *packet,
                          size_t length, struct bt_legacy_adv_request *request);
/* Returns the rewritten event size, or zero for an unrelated event. */
size_t bt_legacy_adv_complete(struct bt_legacy_adv *state,
                              const struct bt_legacy_adv_request *request,
                              uint8_t *event, size_t length, size_t capacity);

#endif
