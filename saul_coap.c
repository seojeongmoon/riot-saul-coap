/*
 * Copyright (C) 2019 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     pkg
 * @{
 *
 * @file
 * @brief       CoAP endpoint for the SAUL registry
 *
 * @author      Micha Rosenbaum <micha.rosenbaum@haw-hamburg.de>
 *
 * @}
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "saul_reg.h"
#include "fmt.h"
#include "net/gcoap.h"

static ssize_t _saul_cnt_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _saul_dev_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _saul_sensortype_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _sense_type_responder(coap_pkt_t* pdu, uint8_t *buf, size_t len, uint8_t type);
/* specific sense type handlers, shortcut via enum in saul.h */
static ssize_t _sense_temp_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _sense_hum_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _sense_servo_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _sense_press_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _sense_voltage_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);

/* CoAP resources. Must be sorted by path (ASCII order). */
static const coap_resource_t _resources[] = {
    { "/hum", COAP_GET, _sense_hum_handler, NULL },
    { "/press", COAP_GET, _sense_press_handler, NULL },
    { "/saul/cnt", COAP_GET, _saul_cnt_handler, NULL },
    { "/saul/dev", COAP_POST, _saul_dev_handler, NULL },
    { "/sensor", COAP_GET, _saul_sensortype_handler, NULL },
    { "/servo", COAP_GET, _sense_servo_handler, NULL },
    { "/temp", COAP_GET, _sense_temp_handler, NULL },
    { "/voltage", COAP_GET, _sense_voltage_handler, NULL },
};

static gcoap_listener_t _listener = {
    &_resources[0],
    sizeof(_resources) / sizeof(_resources[0]),
    NULL,
    NULL
};

static ssize_t _saul_dev_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    int pos = 0;

    (void)ctx;

    if (pdu->payload_len <= 5) {
        char req_payl[6] = { 0 };
        memcpy(req_payl, (char *)pdu->payload, pdu->payload_len);
        pos = strtoul(req_payl, NULL, 10);
    }
    else {
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }

    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    saul_reg_t *dev = saul_reg_find_nth(pos);

    if (!dev) {
        char *err = "device not found";

        if (pdu->payload_len >= strlen(err)) {
            memcpy(pdu->payload, err, strlen(err));
            gcoap_response(pdu, buf, len, COAP_CODE_404);
            return resp_len + strlen(err);
        }
        else {
            puts("saul_coap: msg buffer too small for payload");
            return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
        }
    }
    else {
        char *payl = NULL;
        const char *class_str = saul_class_to_str(dev->driver->type);
        const char *dev_name = dev->name;

        size_t class_size = strlen(class_str);
        size_t dev_size = strlen(dev_name);
        size_t pos_size = sizeof(pos);
        size_t payl_size = class_size + dev_size + pos_size;

        // TODO: Check if payl_size + 1 must be there
        snprintf(payl, payl_size, "%i,%s,%s\n", pos,
            class_str, dev_name);

        if (pdu->payload_len >= strlen(payl)) {
            memcpy(pdu->payload, payl, strlen(payl));
            free(payl);
            gcoap_response(pdu, buf, len, COAP_CODE_204);
            return resp_len + strlen(payl);
        }
        else {
            printf("saul_coap: msg buffer (size: %d) too small"
                   " for payload (size: %d)\n",
                   pdu->payload_len, strlen(payl));
            free(payl);
            return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
        }
    }
}

static ssize_t _saul_cnt_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    saul_reg_t *dev = saul_reg;
    int i = 0;

    (void)ctx;

    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    while (dev) {
        i++;
        dev = dev->next;
    }

    resp_len += fmt_u16_dec((char *)pdu->payload, i);
    return resp_len;
}

static ssize_t _saul_sensortype_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    unsigned char query[NANOCOAP_URI_MAX] = { 0 };
    char type_number[4] = { 0 };
    uint8_t type;

    (void)ctx;

    int size = coap_get_uri_query(pdu, query);

    // FIXME: extract the type number from the query, which has to
    // have the format `&class=123`; read number value from class key
    if (size < 9 || size > 11) {
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }
    strncpy(type_number, (char *)query+7, 3);

    type = atoi(type_number);

    return _sense_type_responder(pdu, buf, len, type);
}

static ssize_t _sense_type_responder(coap_pkt_t* pdu, uint8_t *buf, size_t len, uint8_t type)
{
    saul_reg_t *dev = saul_reg_find_type(type);
    phydat_t res;
    int dim;
    size_t resp_len;

    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    if (dev == NULL) {
        char *err = "device not found";
        if (pdu->payload_len >= strlen(err)) {
            memcpy(pdu->payload, err, strlen(err));
            resp_len += gcoap_response(pdu, buf, len, COAP_CODE_404);
            return resp_len;
        }
        else {
            return gcoap_response(pdu, buf, len, COAP_CODE_404);
        }
    }

    dim = saul_reg_read(dev, &res);
    if (dim <= 0) {
        char *err = "no values found";
        if (pdu->payload_len >= strlen(err)) {
            memcpy(pdu->payload, err, strlen(err));
            resp_len += gcoap_response(pdu, buf, len, COAP_CODE_404);
            return resp_len;
        }
        else {
            return gcoap_response(pdu, buf, len, COAP_CODE_404);
        }
    }

    /* TODO: Take care of all values. */
    /* for (uint8_t i = 0; i < dim; i++) {
       } */

    /* write the response buffer with the request device value */
    resp_len += fmt_u16_dec((char *)pdu->payload, res.val[0]);
    return resp_len;
}

static ssize_t _sense_temp_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    return _sense_type_responder(pdu, buf, len, SAUL_SENSE_TEMP);
}

static ssize_t _sense_hum_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    return _sense_type_responder(pdu, buf, len, SAUL_SENSE_HUM);
}

static ssize_t _sense_servo_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    return _sense_type_responder(pdu, buf, len, SAUL_ACT_SERVO);
}

static ssize_t _sense_press_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    return _sense_type_responder(pdu, buf, len, SAUL_SENSE_PRESS);
}

static ssize_t _sense_voltage_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    return _sense_type_responder(pdu, buf, len, SAUL_SENSE_VOLTAGE);
}

void saul_coap_init(void)
{
    gcoap_register_listener(&_listener);
}
