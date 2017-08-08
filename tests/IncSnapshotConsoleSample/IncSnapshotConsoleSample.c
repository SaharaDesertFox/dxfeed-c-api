// IncSnapshotConsoleSample.cpp : Defines the entry point for the console application.
//

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <string.h>
#include <wctype.h>
#include <stdlib.h>
#define stricmp strcasecmp
#endif

#include "DXFeed.h"
#include "DXErrorCodes.h"
#include <stdio.h>
#include <time.h>

#define RECORDS_PRINT_LIMIT 7
#define MAX_SOURCE_SIZE 20

typedef int bool;

#define true 1
#define false 0

dxf_const_string_t dx_event_type_to_string(int event_type) {
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
        case DXF_ET_GREEKS: return L"Greeks";
        case DXF_ET_SERIES: return L"Series";
        case DXF_ET_CONFIGURATION: return L"Configuration";
        default: return L"";
    }
}

/* -------------------------------------------------------------------------- */
#ifdef _WIN32
static bool is_listener_thread_terminated = false;
CRITICAL_SECTION listener_thread_guard;

bool is_thread_terminate() {
    bool res;
    EnterCriticalSection(&listener_thread_guard);
    res = is_listener_thread_terminated;
    LeaveCriticalSection(&listener_thread_guard);

    return res;
}
#else
static volatile bool is_listener_thread_terminated = false;
bool is_thread_terminate() {
    bool res;
    res = is_listener_thread_terminated;
    return res;
}
#endif


/* -------------------------------------------------------------------------- */

#ifdef _WIN32
void on_reader_thread_terminate(dxf_connection_t connection, void* user_data) {
    EnterCriticalSection(&listener_thread_guard);
    is_listener_thread_terminated = true;
    LeaveCriticalSection(&listener_thread_guard);

    wprintf(L"\nTerminating listener thread\n");
}
#else
void on_reader_thread_terminate(dxf_connection_t connection, void* user_data) {
    is_listener_thread_terminated = true;
    wprintf(L"\nTerminating listener thread\n");
}
#endif

void print_timestamp(dxf_long_t timestamp){
    wchar_t timefmt[80];

    struct tm * timeinfo;
    time_t tmpint = (time_t)(timestamp / 1000);
    timeinfo = localtime(&tmpint);
    wcsftime(timefmt, 80, L"%Y%m%d-%H%M%S", timeinfo);
    wprintf(L"%ls", timefmt);
}

/* -------------------------------------------------------------------------- */

void process_last_error() {
    int error_code = dx_ec_success;
    dxf_const_string_t error_descr = NULL;
    int res;

    res = dxf_get_last_error(&error_code, &error_descr);

    if (res == DXF_SUCCESS) {
        if (error_code == dx_ec_success) {
            wprintf(L"no error information is stored");
            return;
        }

        wprintf(L"Error occurred and successfully retrieved:\n"
            L"error code = %d, description = \"%ls\"\n",
            error_code, error_descr);
        return;
    }

    wprintf(L"An error occurred but the error subsystem failed to initialize\n");
}

/* -------------------------------------------------------------------------- */

dxf_string_t ansi_to_unicode(const char* ansi_str) {
#ifdef _WIN32
    size_t len = strlen(ansi_str);
    dxf_string_t wide_str = NULL;

    // get required size
    int wide_size = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, ansi_str, (int)len, wide_str, 0);

    if (wide_size > 0) {
        wide_str = calloc(wide_size + 1, sizeof(dxf_char_t));
        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, ansi_str, (int)len, wide_str, wide_size);
    }

    return wide_str;
#else /* _WIN32 */
    dxf_string_t wide_str = NULL;
    size_t wide_size = mbstowcs(NULL, ansi_str, 0);
    if (wide_size > 0) {
        wide_str = calloc(wide_size + 1, sizeof(dxf_char_t));
        mbstowcs(wide_str, ansi_str, wide_size + 1);
    }

    return wide_str; /* todo */
#endif /* _WIN32 */
}

/* -------------------------------------------------------------------------- */

void listener(const dxf_snapshot_data_ptr_t snapshot_data, bool new_snapshot, void* user_data) {
    size_t i;
    size_t records_count = snapshot_data->records_count;

    wprintf(L"Snapshot %ls{symbol=%ls, records_count=%zu, type=%ls}\n", 
            dx_event_type_to_string(snapshot_data->event_type), snapshot_data->symbol, 
            records_count,
            new_snapshot ? L"full": L"update");

    if (snapshot_data->event_type == DXF_ET_ORDER) {
        dxf_order_t* order_records = (dxf_order_t*)snapshot_data->records;
        for (i = 0; i < records_count; ++i) {
            dxf_order_t order = order_records[i];

            if (i >= RECORDS_PRINT_LIMIT) {
                wprintf(L"   { ... %zu records left ...}\n", records_count - i);
                break;
            }

            if (!DXF_IS_ORDER_REMOVAL(&order)) {
                wprintf(L"   {index=0x%llX, side=%i, level=%i, time=",
                    order.index, order.side, order.level);
                print_timestamp(order.time);
                wprintf(L", exchange code=%c, market maker=%ls, price=%f, size=%lld",
                    order.exchange_code, order.market_maker, order.price, order.size);
                if (wcslen(order.source) > 0)
                    wprintf(L", source=%ls", order.source);
                wprintf(L", count=%d}\n", order.count);
            } else {
                wprintf(L"   {index=0x%llX, REMOVAL}\n", order.index);
            }
        }
    } else if (snapshot_data->event_type == DXF_ET_CANDLE) {
        dxf_candle_t* candle_records = (dxf_candle_t*)snapshot_data->records;
        for (i = 0; i < snapshot_data->records_count; ++i) {
            dxf_candle_t candle = candle_records[i];

            if (i >= RECORDS_PRINT_LIMIT) {
                wprintf(L"   { ... %zu records left ...}\n", records_count - i);
                break;
            }

            if (!DXF_IS_CANDLE_REMOVAL(&candle)) {
                wprintf(L"    {time=");
                print_timestamp(candle.time);
                wprintf(L", sequence=%d, count=%f, open=%f, high=%f, low=%f, close=%f, volume=%f, "
                    L"VWAP=%f, bidVolume=%f, askVolume=%f}\n",
                    candle.sequence, candle.count, candle.open, candle.high,
                    candle.low, candle.close, candle.volume, candle.vwap,
                    candle.bid_volume, candle.ask_volume);
            } else {
                wprintf(L"    {time=");
                print_timestamp(candle.time);
                wprintf(L", sequence=%d, REMOVAL}\n", candle.sequence);
            }
        }
    } else if (snapshot_data->event_type == DXF_ET_SPREAD_ORDER) {
        dxf_spread_order_t* order_records = (dxf_spread_order_t*)snapshot_data->records;
        for (i = 0; i < records_count; ++i) {
            dxf_spread_order_t order = order_records[i];

            if (i >= RECORDS_PRINT_LIMIT) {
                wprintf(L"   { ... %zu records left ...}\n", records_count - i);
                break;
            }

            if (!DXF_IS_SPREAD_ORDER_REMOVAL(&order)) {
                wprintf(L"   {index=0x%llX, side=%i, level=%i, time=",
                    order.index, order.side, order.level);
                print_timestamp(order.time);
                wprintf(L", sequence=%i, exchange code=%c, price=%f, size=%lld, source=%ls, "
                    L"count=%i, flags=%i, spread symbol=%ls}\n",
                    order.sequence, order.exchange_code, order.price, order.size,
                    wcslen(order.source) > 0 ? order.source : L"",
                    order.count, order.event_flags,
                    wcslen(order.spread_symbol) > 0 ? order.spread_symbol : L"");
            } else {
                wprintf(L"   {index=0x%llX, REMOVAL}\n", order.index);
            }
        }
    } else if (snapshot_data->event_type == DXF_ET_TIME_AND_SALE) {
        dxf_time_and_sale_t* time_and_sale_records = (dxf_time_and_sale_t*)snapshot_data->records;
        for (i = 0; i < snapshot_data->records_count; ++i) {
            dxf_time_and_sale_t tns = time_and_sale_records[i];

            if (i >= RECORDS_PRINT_LIMIT) {
                wprintf(L"   { ... %zu records left ...}\n", records_count - i);
                break;
            }

            if (!DXF_IS_TIME_AND_SALE_REMOVAL(&tns)) {
                wprintf(L"    {time=");
                print_timestamp(tns.time);
                wprintf(L", sequence=%d, event id=%I64i, exchange code=%c, price=%f, size=%I64i, bid price=%f, ask price=%f, "
                    L"exchange sale conditions=\'%ls\', is trade=%ls, type=%i}\n",
                    tns.sequence, tns.event_id, tns.exchange_code, tns.price, tns.size,
                    tns.bid_price, tns.ask_price, tns.exchange_sale_conditions,
                    tns.is_trade ? L"True" : L"False", tns.type);
            } else {
                wprintf(L"    {time=");
                print_timestamp(tns.time);
                wprintf(L", sequence=%d, REMOVAL}\n", tns.sequence);
            }
        }
    } else if (snapshot_data->event_type == DXF_ET_GREEKS) {
        dxf_greeks_t* greeks_records = (dxf_greeks_t*)snapshot_data->records;
        for (i = 0; i < snapshot_data->records_count; ++i) {
            dxf_greeks_t grks = greeks_records[i];

            if (i >= RECORDS_PRINT_LIMIT) {
                wprintf(L"   { ... %zu records left ...}\n", records_count - i);
                break;
            }

            if (!DXF_IS_GREEKS_REMOVAL(&grks)) {
                wprintf(L"    {time=");
                print_timestamp(grks.time);
                wprintf(L", sequence=%d, greeks price=%f, volatility=%f, "
                    L"delta=%f, gamma=%f, theta=%f, rho=%f, vega=%f, index=0x%I64X}\n",
                    grks.sequence, grks.greeks_price, grks.volatility, grks.delta,
                    grks.gamma, grks.theta, grks.rho, grks.vega, grks.index);
            } else {
                wprintf(L"    {time=");
                print_timestamp(grks.time);
                wprintf(L", sequence=%d, REMOVAL}\n", grks.sequence);
            }
        }
    } else if (snapshot_data->event_type == DXF_ET_SERIES) {
        dxf_series_t* series_records = (dxf_series_t*)snapshot_data->records;
        for (i = 0; i < snapshot_data->records_count; ++i) {
            dxf_series_t srs = series_records[i];

            if (i >= RECORDS_PRINT_LIMIT) {
                wprintf(L"   { ... %zu records left ...}\n", records_count - i);
                break;
            }
            if (!DXF_IS_SERIES_REMOVAL(&srs)) {
                wprintf(L"expiration=%d, sequence=%d, volatility=%f, put call ratio=%f, "
                L"forward_price=%f, dividend=%f, interest=%f, index=0x%I64X}\n",
                srs.expiration, srs.sequence, srs.volatility, srs.put_call_ratio,
                srs.forward_price, srs.dividend, srs.interest, srs.index);
            } else {
                wprintf(L"    {expiration=%d, sequence=%d, REMOVAL}\n", srs.expiration, srs.sequence);
            }
        }
    }
}

/* -------------------------------------------------------------------------- */

int main(int argc, char* argv[]) {
    dxf_connection_t connection;
    dxf_snapshot_t snapshot;
    dxf_candle_attributes_t candle_attributes = NULL;
    char* event_type_name = NULL;
    dx_event_id_t event_id;
    dxf_string_t base_symbol = NULL;
    char* dxfeed_host = NULL;
    dxf_string_t dxfeed_host_u = NULL;
    char order_source[MAX_SOURCE_SIZE + 1] = { 0 };
    char* order_source_ptr = NULL;
    char* param_ptr = NULL;
    size_t string_len = 0;

    if (argc < 4) {
        wprintf(L"DXFeed command line sample.\n"
            L"Usage: IncSnapshotConsoleSample <server address> <event type> <symbol> [order_source]\n"
            L"  <server address> - a DXFeed server address, e.g. demo.dxfeed.com:7300\n"
            L"  <event type> - an event type, one of the following: ORDER, CANDLE, SPREAD_ORDER,\n"
            L"                 TIME_AND_SALE, GREEKS, SERIES\n"
            L"  <symbol> - a trade symbol, e.g. C, MSFT, YHOO, IBM\n"
            L"  [order_source] - a) source for Order (also can be empty), e.g. NTV, BYX, BZX, DEA,\n"
            L"                      ISE, DEX, IST\n"
            L"                   b) source for MarketMaker, one of following: COMPOSITE_BID or \n"
            L"                      COMPOSITE_ASK\n");
        
        return 0;
    }

    dxf_initialize_logger("log.log", true, true, true);

    dxfeed_host = argv[1];

    event_type_name = argv[2];
    if (stricmp(event_type_name, "ORDER") == 0) {
        event_id = dx_eid_order;
    } else if (stricmp(event_type_name, "CANDLE") == 0) {
        event_id = dx_eid_candle;
    } else if (stricmp(event_type_name, "SPREAD_ORDER") == 0) {
        event_id = dx_eid_spread_order;
    } else if (stricmp(event_type_name, "TIME_AND_SALE") == 0) {
        event_id = dx_eid_time_and_sale;
    } else if (stricmp(event_type_name, "GREEKS") == 0) {
        event_id = dx_eid_greeks;
    } else if (stricmp(event_type_name, "SERIES") == 0) {
        event_id = dx_eid_series;
    } else {
        wprintf(L"Unknown event type.\n");
        return -1;
    }

    base_symbol = ansi_to_unicode(argv[3]);
    if (base_symbol == NULL) {
        return -1;
    }
    else {
        int i = 0;
        for (; base_symbol[i]; i++)
            base_symbol[i] = towupper(base_symbol[i]);
    }

    if (argc == 5) {
        param_ptr = argv[4];
        string_len = strlen(param_ptr);
        if (string_len > MAX_SOURCE_SIZE) {
            wprintf(L"Error: Invalid order source param!\n");
            return -1;
        }
        strcpy(order_source, param_ptr);
        order_source_ptr = &(order_source[0]);
    }

    wprintf(L"IncSnapshotConsoleSample test started.\n");
    dxfeed_host_u = ansi_to_unicode(dxfeed_host);
    wprintf(L"Connecting to host %ls...\n", dxfeed_host_u);
    free(dxfeed_host_u);

#ifdef _WIN32
    InitializeCriticalSection(&listener_thread_guard);
#endif

    if (!dxf_create_connection(dxfeed_host, on_reader_thread_terminate, NULL, NULL, NULL, &connection)) {
        process_last_error();
        return -1;
    }

    wprintf(L"Connection successful!\n");

    if (event_id == dx_eid_candle) {
        if (!dxf_create_candle_symbol_attributes(base_symbol,
            DXF_CANDLE_EXCHANGE_CODE_ATTRIBUTE_DEFAULT,
            DXF_CANDLE_PERIOD_VALUE_ATTRIBUTE_DEFAULT,
            dxf_ctpa_day, dxf_cpa_default, dxf_csa_default,
            dxf_caa_default, &candle_attributes)) {

            process_last_error();
            dxf_close_connection(connection);
            return -1;
        }

        if (!dxf_create_candle_snapshot(connection, candle_attributes, 0, &snapshot)) {
            process_last_error();
            dxf_delete_candle_symbol_attributes(candle_attributes);
            dxf_close_connection(connection);
            return -1;
        }
    } else if (event_id == dx_eid_order) {
        if (!dxf_create_order_snapshot(connection, base_symbol, order_source_ptr, 0, &snapshot)) {
            process_last_error();
            dxf_close_connection(connection);
            return -1;
        }
    } else {
        if (!dxf_create_snapshot(connection, event_id, base_symbol, NULL, 0, &snapshot)) {
            process_last_error();
            dxf_close_connection(connection);
            return -1;
        }
    }

    if (!dxf_attach_snapshot_inc_listener(snapshot, listener, NULL)) {
        process_last_error();
        if (candle_attributes != NULL)
            dxf_delete_candle_symbol_attributes(candle_attributes);
        dxf_close_connection(connection);
        return -1;
    };
    wprintf(L"Subscription successful!\n");

    while (!is_thread_terminate()) {
#ifdef _WIN32
        Sleep(100);
#else
        sleep(1);
#endif
    }

    if (!dxf_close_snapshot(snapshot)) {
        process_last_error();
        if (candle_attributes != NULL)
            dxf_delete_candle_symbol_attributes(candle_attributes);
        dxf_close_connection(connection);
        return -1;
    }

    if (!dxf_delete_candle_symbol_attributes(candle_attributes)) {
        process_last_error();
        dxf_close_connection(connection);
        return -1;
    }

    wprintf(L"Disconnecting from host...\n");

    if (!dxf_close_connection(connection)) {
        process_last_error();

        return -1;
    }

    wprintf(L"Disconnect successful!\nConnection test completed successfully!\n");

#ifdef _WIN32
    DeleteCriticalSection(&listener_thread_guard);
#endif

    return 0;
}
