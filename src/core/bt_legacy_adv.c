/* SPDX-License-Identifier: MIT */
#include "bt_legacy_adv.h"

enum {
    LE_SET_RANDOM_ADDRESS = 0x2005,
    LE_SET_ADV_PARAMETERS = 0x2006,
    LE_READ_ADV_TX_POWER = 0x2007,
    LE_SET_ADV_DATA = 0x2008,
    LE_SET_SCAN_RESPONSE = 0x2009,
    LE_SET_ADV_ENABLE = 0x200a,
    LE_SET_ADV_SET_RANDOM_ADDRESS = 0x2035,
    LE_SET_EXT_ADV_PARAMETERS = 0x2036,
    LE_SET_EXT_ADV_DATA = 0x2037,
    LE_SET_EXT_SCAN_RESPONSE = 0x2038,
    LE_SET_EXT_ADV_ENABLE = 0x2039,
    LE_READ_MAX_ADV_DATA_LENGTH = 0x203a,
    LE_READ_ADV_SET_COUNT = 0x203b,
    LE_REMOVE_ADV_SET = 0x203c,
    LE_CLEAR_ADV_SETS = 0x203d,
    HCI_CAPACITY_EXCEEDED = 0x07,
    HCI_COMMAND_DISALLOWED = 0x0c,
    HCI_UNSUPPORTED = 0x11,
    HCI_INVALID_PARAMETERS = 0x12,
    HCI_UNKNOWN_ADV_ID = 0x42,
    ADV_DATA_MAX = 31,
    ADV_IND = 0,
    ADV_SCAN_IND = 2,
    ADV_NONCONN_IND = 3,
};

int bt_legacy_adv_usb_match(uint16_t vendor, uint16_t product)
{
    return (vendor == 0x0bda && product == 0xb00a) ||
           (vendor == 0x13d3 && (product == 0x3529 || product == 0x3533 || product == 0x3558));
}

void bt_legacy_adv_reset(struct bt_legacy_adv *state)
{
    /* Also reset the physical slot's remembered scan response and TX power. */
    *state = (struct bt_legacy_adv){0};
}

static uint16_t le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

static void set_command(struct bt_legacy_adv_request *request, uint16_t opcode,
                        uint8_t parameter_length)
{
    request->command[0] = (uint8_t)opcode;
    request->command[1] = (uint8_t)(opcode >> 8);
    request->command[2] = parameter_length;
    request->length = (uint8_t)(parameter_length + 3);
}

static uint8_t handle_status(const struct bt_legacy_adv *state, uint8_t handle)
{
    return state->configured && state->handle == handle ? 0 : HCI_UNKNOWN_ADV_ID;
}

static uint8_t set_parameters(const struct bt_legacy_adv *state, const uint8_t *p,
                              struct bt_legacy_adv_request *request)
{
    if (p[0] > 0xef) return HCI_INVALID_PARAMETERS;
    if (state->configured && state->handle != p[0]) return HCI_CAPACITY_EXCEEDED;
    if (state->enabled) return HCI_COMMAND_DISALLOWED;

    uint8_t type;
    switch (le16(p + 1)) {
    case 0x0000:
    case 0x0010: type = ADV_NONCONN_IND; break;
    case 0x0002:
    case 0x0012: type = ADV_SCAN_IND; break;
    case 0x0001: /* Legacy connectable undirected PDUs are also scannable. */
    case 0x0013: type = ADV_IND; break;
    default: return HCI_UNSUPPORTED;
    }
    if (p[5] || p[8] || p[19] != 0x7f || p[20] != 1 || p[24])
        return HCI_UNSUPPORTED;

    set_command(request, LE_SET_ADV_PARAMETERS, 15);
    uint8_t *q = request->command + 3;
    q[0] = p[3]; q[1] = p[4];     /* 24-bit intervals fit the legacy 16-bit fields */
    q[2] = p[6]; q[3] = p[7];
    q[4] = type;
    q[5] = p[10]; q[6] = p[11];  /* own and peer address types */
    for (size_t i = 0; i < 6; i++) q[7 + i] = p[12 + i];
    q[13] = p[9]; q[14] = p[18]; /* primary channel map and filter policy */
    request->handle = p[0];
    return 0;
}

static uint8_t set_data(const struct bt_legacy_adv *state, const uint8_t *p, size_t length,
                        struct bt_legacy_adv_request *request)
{
    if (length < 4 || length != (size_t)p[3] + 4) return HCI_INVALID_PARAMETERS;
    uint8_t status = handle_status(state, p[0]);
    if (status) return status;
    if (p[1] != 0x03 || p[3] > ADV_DATA_MAX) return HCI_UNSUPPORTED;

    uint16_t opcode = request->opcode == LE_SET_EXT_ADV_DATA ? LE_SET_ADV_DATA : LE_SET_SCAN_RESPONSE;
    set_command(request, opcode, ADV_DATA_MAX + 1);
    request->command[3] = p[3];
    for (size_t i = 0; i < p[3]; i++) request->command[4 + i] = p[4 + i];
    return 0;
}

static uint8_t set_enabled(const struct bt_legacy_adv *state, const uint8_t *p, size_t length,
                           struct bt_legacy_adv_request *request)
{
    if (length < 2 || p[0] > 1 || length != 2u + 4u * p[1])
        return HCI_INVALID_PARAMETERS;
    if (p[1] > 1) return HCI_CAPACITY_EXCEEDED;
    if (p[0] && !p[1]) return HCI_INVALID_PARAMETERS;
    if (p[1]) {
        uint8_t status = handle_status(state, p[2]);
        if (status) return status;
        if (p[0] && (p[3] || p[4] || p[5])) return HCI_UNSUPPORTED;
    }
    if (p[0] && (!state->data_ready || (state->type != ADV_NONCONN_IND &&
                       state->scan_response_present && !state->scan_response_ready)))
        return HCI_COMMAND_DISALLOWED;
    set_command(request, LE_SET_ADV_ENABLE, 1);
    request->command[3] = p[0];
    return 0;
}

int bt_legacy_adv_prepare(const struct bt_legacy_adv *state, const uint8_t *packet,
                          size_t length, struct bt_legacy_adv_request *request)
{
    if (length < 3 || length != (size_t)packet[2] + 3) return 0;
    uint16_t opcode = le16(packet);
    if (opcode < LE_SET_ADV_SET_RANDOM_ADDRESS || opcode > LE_CLEAR_ADV_SETS) return 0;

    *request = (struct bt_legacy_adv_request){ .opcode = opcode };
    /* Read-only placeholder for commands handled in software. Its real
     * Command Complete provides the controller's command credit. */
    set_command(request, LE_READ_ADV_TX_POWER, 0);
    const uint8_t *p = packet + 3;
    const uint8_t n = packet[2];
    switch (opcode) {
    case LE_SET_ADV_SET_RANDOM_ADDRESS:
        request->status = n == 7 ? handle_status(state, p[0]) : HCI_INVALID_PARAMETERS;
        if (request->status == 0) {
            set_command(request, LE_SET_RANDOM_ADDRESS, 6);
            for (size_t i = 0; i < 6; i++) request->command[3 + i] = p[1 + i];
        }
        break;
    case LE_SET_EXT_ADV_PARAMETERS:
        request->response_length = 1;
        request->response[0] = state->tx_power;
        request->status = n == 25 ? set_parameters(state, p, request) : HCI_INVALID_PARAMETERS;
        break;
    case LE_SET_EXT_ADV_DATA:
    case LE_SET_EXT_SCAN_RESPONSE:
        request->status = set_data(state, p, n, request);
        break;
    case LE_SET_EXT_ADV_ENABLE:
        request->status = set_enabled(state, p, n, request);
        break;
    case LE_READ_MAX_ADV_DATA_LENGTH:
        request->response_length = 2;
        request->response[0] = ADV_DATA_MAX;
        request->status = n == 0 ? 0 : HCI_INVALID_PARAMETERS;
        break;
    case LE_READ_ADV_SET_COUNT:
        request->response_length = 1;
        request->response[0] = 1;
        request->status = n == 0 ? 0 : HCI_INVALID_PARAMETERS;
        break;
    case LE_REMOVE_ADV_SET:
        request->status = n == 1 ? handle_status(state, p[0]) : HCI_INVALID_PARAMETERS;
        if (request->status == 0 && state->enabled) request->status = HCI_COMMAND_DISALLOWED;
        break;
    case LE_CLEAR_ADV_SETS:
        request->status = n != 0 ? HCI_INVALID_PARAMETERS : state->enabled ? HCI_COMMAND_DISALLOWED : 0;
        break;
    }
    return 1;
}

size_t bt_legacy_adv_complete(struct bt_legacy_adv *state,
                              const struct bt_legacy_adv_request *request,
                              uint8_t *event, size_t length, size_t capacity)
{
    const size_t needed = 6u + request->response_length;
    const uint16_t sent = le16(request->command);
    if (length < 6 || length != (size_t)event[1] + 2 || capacity < needed)
        return 0;

    uint8_t credit;
    uint8_t controller_status;
    if (event[0] == 0x0e && le16(event + 3) == sent) {
        credit = event[2];
        controller_status = event[5];
    } else if (event[0] == 0x0f && length == 6 && event[2] != 0 && le16(event + 4) == sent) {
        credit = event[3];
        controller_status = event[2];
    } else {
        return 0;
    }

    if (sent == LE_READ_ADV_TX_POWER && length == 7 && controller_status == 0)
        state->tx_power = event[6];
    const uint8_t status = request->status ? request->status : controller_status;
    if (status == 0) {
        switch (request->opcode) {
        case LE_SET_EXT_ADV_PARAMETERS:
            state->configured = 1;
            state->handle = request->handle;
            state->type = request->command[7];
            break;
        case LE_SET_EXT_ADV_DATA:
            state->data_ready = 1;
            break;
        case LE_SET_EXT_SCAN_RESPONSE:
            state->scan_response_ready = 1;
            state->scan_response_present = request->command[3] != 0;
            break;
        case LE_SET_EXT_ADV_ENABLE:
            state->enabled = request->command[3];
            break;
        case LE_REMOVE_ADV_SET:
        case LE_CLEAR_ADV_SETS:
            state->configured = state->data_ready = state->scan_response_ready = 0;
            break;
        }
    }

    event[0] = 0x0e;
    event[1] = (uint8_t)(needed - 2);
    event[2] = credit;
    event[3] = (uint8_t)request->opcode;
    event[4] = (uint8_t)(request->opcode >> 8);
    event[5] = status;
    for (size_t i = 0; i < request->response_length; i++)
        event[6 + i] = status == 0 ? request->response[i] : 0;
    return needed;
}
