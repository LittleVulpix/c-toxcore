/* tox.c
 *
 * The Tox public API.
 *
 *  Copyright (C) 2013 Tox project All Rights Reserved.
 *
 *  This file is part of Tox.
 *
 *  Tox is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Tox is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Tox.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "Messenger.h"
#include "group.h"
#include "logger.h"

#include "../toxencryptsave/defines.h"

#define TOX_DEFINED
typedef struct Messenger Tox;

#include "tox.h"

#define SET_ERROR_PARAMETER(param, x) {if(param) {*param = x;}}

uint32_t tox_version_major(void)
{
    return 0;
}

uint32_t tox_version_minor(void)
{
    return 0;
}

uint32_t tox_version_patch(void)
{
    return 0;
}

bool tox_version_is_compatible(uint32_t major, uint32_t minor, uint32_t patch)
{
    //TODO
    return 1;
}


void tox_options_default(struct Tox_Options *options)
{
    if (options) {
        memset(options, 0, sizeof(struct Tox_Options));
        options->ipv6_enabled = 1;
        options->udp_enabled = 1;
        options->proxy_type = TOX_PROXY_TYPE_NONE;
    }
}

struct Tox_Options *tox_options_new(TOX_ERR_OPTIONS_NEW *error)
{
    struct Tox_Options *options = calloc(sizeof(struct Tox_Options), 1);

    if (options) {
        SET_ERROR_PARAMETER(error, TOX_ERR_OPTIONS_NEW_OK);
        return options;
    }

    SET_ERROR_PARAMETER(error, TOX_ERR_OPTIONS_NEW_MALLOC);
    return NULL;
}

void tox_options_free(struct Tox_Options *options)
{
    free(options);
}

Tox *tox_new(struct Tox_Options const *options, uint8_t const *data, size_t length, TOX_ERR_NEW *error)
{
    if (!logger_get_global())
        logger_set_global(logger_new(LOGGER_OUTPUT_FILE, LOGGER_LEVEL, "toxcore"));

    if (data) {
        if (memcmp(data, TOX_ENC_SAVE_MAGIC_NUMBER, TOX_ENC_SAVE_MAGIC_LENGTH) == 0) {
            SET_ERROR_PARAMETER(error, TOX_ERR_NEW_LOAD_ENCRYPTED);
            return NULL;
        }
    }

    Messenger_Options m_options = {0};

    if (options == NULL) {
        m_options.ipv6enabled = TOX_ENABLE_IPV6_DEFAULT;
    } else {
        m_options.ipv6enabled = options->ipv6_enabled;
        m_options.udp_disabled = !options->udp_enabled;

        switch (options->proxy_type) {
            case TOX_PROXY_TYPE_HTTP:
                m_options.proxy_info.proxy_type = TCP_PROXY_HTTP;
                break;

            case TOX_PROXY_TYPE_SOCKS5:
                m_options.proxy_info.proxy_type = TCP_PROXY_SOCKS5;
                break;

            case TOX_PROXY_TYPE_NONE:
                m_options.proxy_info.proxy_type = TCP_PROXY_NONE;
                break;

            default:
                SET_ERROR_PARAMETER(error, TOX_ERR_PROXY_TYPE);
                return NULL;
        }

        if (m_options.proxy_info.proxy_type != TCP_PROXY_NONE) {
            if (options->proxy_port == 0) {
                SET_ERROR_PARAMETER(error, TOX_ERR_NEW_PROXY_BAD_PORT);
                return NULL;
            }

            ip_init(&m_options.proxy_info.ip_port.ip, m_options.ipv6enabled);

            if (m_options.ipv6enabled)
                m_options.proxy_info.ip_port.ip.family = AF_UNSPEC;

            if (!addr_resolve_or_parse_ip(options->proxy_address, &m_options.proxy_info.ip_port.ip, NULL)) {
                SET_ERROR_PARAMETER(error, TOX_ERR_NEW_PROXY_BAD_HOST);
                //TODO: TOX_ERR_NEW_PROXY_NOT_FOUND if domain.
                return NULL;
            }

            m_options.proxy_info.ip_port.port = htons(options->proxy_port);
        }
    }

    Messenger *m = new_messenger(&m_options);
    //TODO: TOX_ERR_NEW_MALLOC
    //TODO: TOX_ERR_NEW_PORT_ALLOC

    if (!new_groupchats(m)) {
        kill_messenger(m);
        SET_ERROR_PARAMETER(error, TOX_ERR_NEW_MALLOC);
        return NULL;
    }

    if (messenger_load(m, data, length) == -1) {
        /* TODO: uncomment this when tox is stable.
        tox_kill(m);
        SET_ERROR_PARAMETER(error, TOX_ERR_NEW_LOAD_BAD_FORMAT);
        return NULL;
        */
    }

    SET_ERROR_PARAMETER(error, TOX_ERR_NEW_OK);
    return m;
}

void tox_kill(Tox *tox)
{
    Messenger *m = tox;
    kill_groupchats(m->group_chat_object);
    kill_messenger(m);
    logger_kill_global();
}

size_t tox_save_size(Tox const *tox)
{
    const Messenger *m = tox;
    return messenger_size(m);
}

void tox_save(Tox const *tox, uint8_t *data)
{
    if (data) {
        const Messenger *m = tox;
        messenger_save(m, data);
    }
}

static int address_to_ip(Messenger *m, char const *address, IP_Port *ip_port, IP_Port *ip_port_v4)
{
    if (!addr_parse_ip(address, &ip_port->ip)) {
        if (m->options.udp_disabled) { /* Disable DNS when udp is disabled. */
            return -1;
        }

        IP *ip_extra = NULL;
        ip_init(&ip_port->ip, m->options.ipv6enabled);

        if (m->options.ipv6enabled && ip_port_v4) {
            /* setup for getting BOTH: an IPv6 AND an IPv4 address */
            ip_port->ip.family = AF_UNSPEC;
            ip_reset(&ip_port_v4->ip);
            ip_extra = &ip_port_v4->ip;
        }

        if (!addr_resolve(address, &ip_port->ip, ip_extra)) {
            return -1;
        }
    }

    return 0;
}

bool tox_bootstrap(Tox *tox, char const *address, uint16_t port, uint8_t const *public_key, TOX_ERR_BOOTSTRAP *error)
{
    if (!address || !public_key) {
        SET_ERROR_PARAMETER(error, TOX_ERR_BOOTSTRAP_NULL);
        return 0;
    }

    Messenger *m = tox;
    bool ret = tox_add_tcp_relay(tox, address, port, public_key, error);

    if (!ret) {
        return 0;
    }

    if (m->options.udp_disabled) {
        return ret;
    } else { /* DHT only works on UDP. */
        if (DHT_bootstrap_from_address(m->dht, address, m->options.ipv6enabled, htons(port), public_key) == 0) {
            SET_ERROR_PARAMETER(error, TOX_ERR_BOOTSTRAP_BAD_ADDRESS);
            return 0;
        }

        SET_ERROR_PARAMETER(error, TOX_ERR_BOOTSTRAP_OK);
        return 1;
    }
}

bool tox_add_tcp_relay(Tox *tox, char const *address, uint16_t port, uint8_t const *public_key,
                       TOX_ERR_BOOTSTRAP *error)
{
    if (!address || !public_key) {
        SET_ERROR_PARAMETER(error, TOX_ERR_BOOTSTRAP_NULL);
        return 0;
    }

    Messenger *m = tox;
    IP_Port ip_port, ip_port_v4;

    if (port == 0) {
        SET_ERROR_PARAMETER(error, TOX_ERR_BOOTSTRAP_BAD_PORT);
        return 0;
    }

    if (address_to_ip(m, address, &ip_port, &ip_port_v4) == -1) {
        SET_ERROR_PARAMETER(error, TOX_ERR_BOOTSTRAP_BAD_ADDRESS);
        return 0;
    }

    ip_port.port = htons(port);
    add_tcp_relay(m->net_crypto, ip_port, public_key);
    onion_add_bs_path_node(m->onion_c, ip_port, public_key); //TODO: move this

    SET_ERROR_PARAMETER(error, TOX_ERR_BOOTSTRAP_OK);
    return 1;
}

TOX_CONNECTION tox_get_connection_status(Tox const *tox)
{
    const Messenger *m = tox;

    if (onion_isconnected(m->onion_c)) {
        if (DHT_non_lan_connected(m->dht)) {
            return TOX_CONNECTION_UDP;
        }

        return TOX_CONNECTION_TCP;
    }

    return TOX_CONNECTION_NONE;
}


void tox_callback_connection_status(Tox *tox, tox_connection_status_cb *function, void *user_data)
{
    //TODO
}

uint32_t tox_iteration_interval(Tox const *tox)
{
    const Messenger *m = tox;
    return messenger_run_interval(m);
}

void tox_iteration(Tox *tox)
{
    Messenger *m = tox;
    do_messenger(m);
    do_groupchats(m->group_chat_object);
}

void tox_self_get_address(Tox const *tox, uint8_t *address)
{
    if (address) {
        const Messenger *m = tox;
        getaddress(m, address);
    }
}

void tox_self_set_nospam(Tox *tox, uint32_t nospam)
{
    Messenger *m = tox;
    set_nospam(&(m->fr), nospam);
}

uint32_t tox_self_get_nospam(Tox const *tox)
{
    const Messenger *m = tox;
    return get_nospam(&(m->fr));
}

void tox_self_get_public_key(Tox const *tox, uint8_t *public_key)
{
    const Messenger *m = tox;

    if (public_key)
        memcpy(public_key, m->net_crypto->self_public_key, crypto_box_PUBLICKEYBYTES);
}

void tox_self_get_private_key(Tox const *tox, uint8_t *private_key)
{
    const Messenger *m = tox;

    if (private_key)
        memcpy(private_key, m->net_crypto->self_secret_key, crypto_box_SECRETKEYBYTES);
}

bool tox_self_set_name(Tox *tox, uint8_t const *name, size_t length, TOX_ERR_SET_INFO *error)
{
    if (!name && length != 0) {
        SET_ERROR_PARAMETER(error, TOX_ERR_SET_INFO_NULL);
        return 0;
    }

    Messenger *m = tox;

    if (setname(m, name, length) == 0) {
        //TODO: function to set different per group names?
        send_name_all_groups(m->group_chat_object);
        SET_ERROR_PARAMETER(error, TOX_ERR_SET_INFO_OK);
        return 1;
    } else {
        SET_ERROR_PARAMETER(error, TOX_ERR_SET_INFO_TOO_LONG);
        return 0;
    }
}

size_t tox_self_get_name_size(Tox const *tox)
{
    const Messenger *m = tox;
    return m_get_self_name_size(m);
}

void tox_self_get_name(Tox const *tox, uint8_t *name)
{
    if (name) {
        const Messenger *m = tox;
        getself_name(m, name);
    }
}

bool tox_self_set_status_message(Tox *tox, uint8_t const *status, size_t length, TOX_ERR_SET_INFO *error)
{
    if (!status && length != 0) {
        SET_ERROR_PARAMETER(error, TOX_ERR_SET_INFO_NULL);
        return 0;
    }

    Messenger *m = tox;

    if (m_set_statusmessage(m, status, length) == 0) {
        SET_ERROR_PARAMETER(error, TOX_ERR_SET_INFO_OK);
        return 1;
    } else {
        SET_ERROR_PARAMETER(error, TOX_ERR_SET_INFO_TOO_LONG);
        return 0;
    }
}

size_t tox_self_get_status_message_size(Tox const *tox)
{
    const Messenger *m = tox;
    return m_get_self_statusmessage_size(m);
}

void tox_self_get_status_message(Tox const *tox, uint8_t *status)
{
    if (status) {
        const Messenger *m = tox;
        m_copy_self_statusmessage(m, status);
    }
}
