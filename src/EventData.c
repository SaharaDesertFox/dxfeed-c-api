/*
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Initial Developer of the Original Code is Devexperts LLC.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 */

#include "EventData.h"
#include "DXAlgorithms.h"
#include "DXErrorHandling.h"
#include "Logger.h"
#include "EventSubscription.h"
#include "DataStructures.h"

/* -------------------------------------------------------------------------- */
/*
 *	Various common data
 */
/* -------------------------------------------------------------------------- */

static const int g_event_data_sizes[dx_eid_count] = {
    sizeof(dxf_trade_t),
    sizeof(dxf_quote_t),
    sizeof(dxf_summary_t),
    sizeof(dxf_profile_t),
    sizeof(dxf_order_t),
    sizeof(dxf_time_and_sale_t),
    sizeof(dxf_candle_t),
    sizeof(dxf_trade_eth_t),
    sizeof(dxf_spread_order_t)
};

static const dxf_char_t g_quote_tmpl[] = L"Quote&";
static const dxf_char_t g_order_tmpl[] = L"Order#";
static const dxf_char_t g_trade_tmpl[] = L"Trade&";
static const dxf_char_t g_summary_tmpl[] = L"Summary&";
static const dxf_char_t g_trade_eth_tmpl[] = L"TradeETH&";

#define STRLEN(char_array) (sizeof(char_array) / sizeof(char_array[0]) - 1)
#define QUOTE_TMPL_LEN STRLEN(g_quote_tmpl)
#define ORDER_TMPL_LEN STRLEN(g_order_tmpl)
#define TRADE_TMPL_LEN STRLEN(g_trade_tmpl)
#define SUMMARY_TMPL_LEN STRLEN(g_summary_tmpl)
#define TRADE_ETH_TMPL_LEN STRLEN(g_trade_eth_tmpl)

/* -------------------------------------------------------------------------- */
/*
 *	Event functions implementation
 */
/* -------------------------------------------------------------------------- */

dxf_const_string_t dx_event_type_to_string (int event_type) {
    switch (event_type) {
    case DXF_ET_TRADE: return L"Trade";
    case DXF_ET_QUOTE: return L"Quote";
    case DXF_ET_SUMMARY: return L"Summary";
    case DXF_ET_PROFILE: return L"Profile";
    case DXF_ET_ORDER: return L"Order";
    case DXF_ET_TIME_AND_SALE: return L"Time&Sale";
    case DXF_ET_CANDLE: return L"Candle";
    case DXF_ET_TRADE_ETH: return L"TradeETH";
    case DXF_ET_SPREAD_ORDER: return L"SpreadOrder";
    default: return L"";
    }
}

/* -------------------------------------------------------------------------- */

int dx_get_event_data_struct_size (int event_id) {
    return g_event_data_sizes[(dx_event_id_t)event_id];
}

/* -------------------------------------------------------------------------- */

dx_event_id_t dx_get_event_id_by_bitmask (int event_bitmask) {
    dx_event_id_t event_id = dx_eid_begin;
    
    if (!dx_is_only_single_bit_set(event_bitmask)) {
        return dx_eid_invalid;
    }
    
    for (; (event_bitmask >>= 1) != 0; ++event_id);
    
    return event_id;
}

/* -------------------------------------------------------------------------- */
/*
 *	Event subscription implementation
 */
/* -------------------------------------------------------------------------- */

bool dx_add_subscription_param_to_list(dxf_connection_t connection, dx_event_subscription_param_list_t* param_list,
                                        dxf_const_string_t record_name, dx_subscription_type_t subscription_type) {
    bool failed = false;
    dx_event_subscription_param_t param;
    dx_record_id_t record_id = dx_add_or_get_record_id(connection, record_name);
    if (record_id < 0) {
        dx_set_last_error(dx_esec_invalid_subscr_id);
        return false;
    }

    param.record_id = record_id;
    param.subscription_type = subscription_type;
    DX_ARRAY_INSERT(*param_list, dx_event_subscription_param_t, param, param_list->size, dx_capacity_manager_halfer, failed);
    if (failed)
        dx_set_last_error(dx_sec_not_enough_memory);
    return !failed;
}

bool dx_get_single_order_subscription_params(dxf_connection_t connection, dx_order_source_array_ptr_t order_source, 
                                             dxf_uint_t subscr_flags,
                                             OUT dx_event_subscription_param_list_t* param_list) {
    dxf_char_t order_name_buf[ORDER_TMPL_LEN + DXF_RECORD_SUFFIX_SIZE] = { 0 };

    if (!IS_FLAG_SET(subscr_flags, DX_SUBSCR_FLAG_SINGLE_RECORD)) {
        return false;
    }
    if (IS_FLAG_SET(subscr_flags, DX_SUBSCR_FLAG_SR_MARKET_MAKER_ORDER)) {
        CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, L"MarketMaker", dx_st_history);
    }
    else {
        if (order_source->size > 1) {
            return false;
        }
        CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, L"Order", dx_st_history);
        if (order_source->size != 0) {
            dx_copy_string(order_name_buf, g_order_tmpl);
            dx_copy_string_len(&order_name_buf[ORDER_TMPL_LEN], order_source->elements[0].suffix, DXF_RECORD_SUFFIX_SIZE);
            CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, order_name_buf, dx_st_history);
        }
    }
    return true;
}

bool dx_get_order_subscription_params(dxf_connection_t connection, dx_order_source_array_ptr_t order_source, 
                                      dxf_uint_t subscr_flags,
                                      OUT dx_event_subscription_param_list_t* param_list) {
    dxf_char_t ch = 'A';
    dxf_char_t order_name_buf[ORDER_TMPL_LEN + DXF_RECORD_SUFFIX_SIZE] = { 0 };
    dxf_char_t quote_name_buf[QUOTE_TMPL_LEN + 2] = { 0 };
    int i;

    if (IS_FLAG_SET(subscr_flags, DX_SUBSCR_FLAG_SINGLE_RECORD)) {
        return dx_get_single_order_subscription_params(connection, order_source, subscr_flags, param_list);
    }

    CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, L"Quote", dx_st_ticker);
    CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, L"MarketMaker", dx_st_history);
    CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, L"Order", dx_st_history);

    dx_copy_string(order_name_buf, g_order_tmpl);
    for (i = 0; i < order_source->size; ++i) {
        dx_copy_string_len(&order_name_buf[ORDER_TMPL_LEN], order_source->elements[i].suffix, DXF_RECORD_SUFFIX_SIZE);
        CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, order_name_buf, dx_st_history);
    }

    /* fill quotes Quote&A..Quote&Z */
    dx_copy_string(quote_name_buf, g_quote_tmpl);
    for (; ch <= 'Z'; ch++) {
        quote_name_buf[QUOTE_TMPL_LEN] = ch;
        CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, quote_name_buf, dx_st_ticker);
    }

    return true;
}

bool dx_get_trade_subscription_params(dxf_connection_t connection, OUT dx_event_subscription_param_list_t* param_list) {
    dxf_char_t ch = 'A';
    dxf_char_t trade_name_buf[TRADE_TMPL_LEN + 2] = { 0 };
    CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, L"Trade", dx_st_ticker);

    /* fill trades Trade&A..Trade&Z */
    dx_copy_string(trade_name_buf, g_trade_tmpl);
    for (; ch <= 'Z'; ch++) {
        trade_name_buf[TRADE_TMPL_LEN] = ch;
        CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, trade_name_buf, dx_st_ticker);
    }

    return true;
}

bool dx_get_summary_subscription_params(dxf_connection_t connection, OUT dx_event_subscription_param_list_t* param_list) {
    dxf_char_t ch = 'A';
    dxf_char_t summary_name_buf[SUMMARY_TMPL_LEN + 2] = { 0 };
    CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, L"Summary", dx_st_ticker);

    /* fill summaries Summary&A..Summary&Z */
    dx_copy_string(summary_name_buf, g_summary_tmpl);
    for (; ch <= 'Z'; ch++) {
        summary_name_buf[SUMMARY_TMPL_LEN] = ch;
        CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, summary_name_buf, dx_st_ticker);
    }

    return true;
}

bool dx_get_trade_eth_subscription_params(dxf_connection_t connection, OUT dx_event_subscription_param_list_t* param_list) {
    dxf_char_t ch = 'A';
    dxf_char_t trade_name_buf[TRADE_ETH_TMPL_LEN + 2] = { 0 };
    CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, L"TradeETH", dx_st_ticker);

    /* fill trades TradeETH&A..TradeETH&Z */
    dx_copy_string(trade_name_buf, g_trade_eth_tmpl);
    for (; ch <= 'Z'; ch++) {
        trade_name_buf[TRADE_ETH_TMPL_LEN] = ch;
        CHECKED_CALL_4(dx_add_subscription_param_to_list, connection, param_list, trade_name_buf, dx_st_ticker);
    }

    return true;
}

/*
 * Returns the list of subscription params. Fills records list according to event_id.
 *
 * You need to call dx_free(params.elements) to free resources.
*/
int dx_get_event_subscription_params(dxf_connection_t connection, dx_order_source_array_ptr_t order_source, dx_event_id_t event_id,
                                     dxf_uint_t subscr_flags, OUT dx_event_subscription_param_list_t* params) {
    bool result = true;
    dx_event_subscription_param_list_t param_list = { NULL, 0, 0 };
    dx_subscription_type_t sub_type;

    switch (event_id) {
    case dx_eid_trade:
        result = dx_get_trade_subscription_params(connection, &param_list);
        break;
    case dx_eid_quote:
        result = dx_add_subscription_param_to_list(connection, &param_list, L"Quote", dx_st_ticker);
        break;
    case dx_eid_summary:
        result = dx_get_summary_subscription_params(connection, &param_list);
        break;
    case dx_eid_profile:
        result = dx_add_subscription_param_to_list(connection, &param_list, L"Profile", dx_st_ticker);
        break;
    case dx_eid_order:
        result = dx_get_order_subscription_params(connection, order_source, subscr_flags, &param_list);
        break;
    case dx_eid_time_and_sale:
        sub_type = IS_FLAG_SET(subscr_flags, DX_SUBSCR_FLAG_TIME_SERIES) ? dx_st_history : dx_st_stream;
        result = dx_add_subscription_param_to_list(connection, &param_list, L"TimeAndSale", sub_type);
        break;
    case dx_eid_candle:
        result = dx_add_subscription_param_to_list(connection, &param_list, L"Candle", dx_st_history);
        break;
    case dx_eid_trade_eth:
        result = dx_get_trade_eth_subscription_params(connection, &param_list);
        break;
    case dx_eid_spread_order:
        result = dx_add_subscription_param_to_list(connection, &param_list, L"SpreadOrder", dx_st_history) &&
            dx_add_subscription_param_to_list(connection, &param_list, L"SpreadOrder#ISE", dx_st_history);
        break;
    }

    if (!result) {
        dx_logging_last_error();
        dx_logging_info(L"Unable to create subscription to event %d (%s)", event_id, dx_event_type_to_string(event_id));
    }

    *params = param_list;
    return param_list.size;
}
    
/* -------------------------------------------------------------------------- */
/*
 *	Event data navigation
 */
/* -------------------------------------------------------------------------- */

typedef const dxf_event_data_t (*dx_event_data_navigator) (const dxf_event_data_t data, int index);
#define EVENT_DATA_NAVIGATOR_NAME(struct_name) \
    struct_name##_data_navigator
    
#define EVENT_DATA_NAVIGATOR_BODY(struct_name) \
    const dxf_event_data_t EVENT_DATA_NAVIGATOR_NAME(struct_name) (const dxf_event_data_t data, int index) { \
        struct_name* buffer = (struct_name*)data; \
        \
        return (const dxf_event_data_t)(buffer + index); \
    }
    
EVENT_DATA_NAVIGATOR_BODY(dxf_trade_t)
EVENT_DATA_NAVIGATOR_BODY(dxf_quote_t)
EVENT_DATA_NAVIGATOR_BODY(dxf_summary_t)
EVENT_DATA_NAVIGATOR_BODY(dxf_profile_t)
EVENT_DATA_NAVIGATOR_BODY(dxf_order_t)
EVENT_DATA_NAVIGATOR_BODY(dxf_time_and_sale_t)
EVENT_DATA_NAVIGATOR_BODY(dxf_candle_t)
EVENT_DATA_NAVIGATOR_BODY(dxf_trade_eth_t)
EVENT_DATA_NAVIGATOR_BODY(dxf_spread_order_t)

static const dx_event_data_navigator g_event_data_navigators[dx_eid_count] = {
    EVENT_DATA_NAVIGATOR_NAME(dxf_trade_t),
    EVENT_DATA_NAVIGATOR_NAME(dxf_quote_t),
    EVENT_DATA_NAVIGATOR_NAME(dxf_summary_t),
    EVENT_DATA_NAVIGATOR_NAME(dxf_profile_t),
    EVENT_DATA_NAVIGATOR_NAME(dxf_order_t),
    EVENT_DATA_NAVIGATOR_NAME(dxf_time_and_sale_t),
    EVENT_DATA_NAVIGATOR_NAME(dxf_candle_t),
    EVENT_DATA_NAVIGATOR_NAME(dxf_trade_eth_t),
    EVENT_DATA_NAVIGATOR_NAME(dxf_spread_order_t)
};

/* -------------------------------------------------------------------------- */

const dxf_event_data_t dx_get_event_data_item (int event_mask, const dxf_event_data_t data, int index) {
    return g_event_data_navigators[dx_get_event_id_by_bitmask(event_mask)](data, index);
}