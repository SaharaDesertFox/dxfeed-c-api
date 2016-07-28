// SnapshotConsoleSample.cpp : Defines the entry point for the console application.
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

void listener(const dxf_snapshot_data_ptr_t snapshot_data, void* user_data) {
    dxf_int_t i;
    int records_count = snapshot_data->records_count;

    wprintf(L"Snapshot %ls{symbol=%ls, records_count=%d}\n", 
            dx_event_type_to_string(snapshot_data->event_type), snapshot_data->symbol, 
            records_count);

    if (snapshot_data->event_type == DXF_ET_ORDER) {
        dxf_order_t* order_records = (dxf_order_t*)snapshot_data->records;
        for (i = 0; i < records_count; ++i) {
            dxf_order_t order = order_records[i];

            if (i >= RECORDS_PRINT_LIMIT) {
                wprintf(L"   { ... %d records left ...}\n", records_count - i);
                break;
            }

            wprintf(L"   {index=0x%llX, side=%i, level=%i, time=",
                order.index, order.side, order.level);
            print_timestamp(order.time);
            wprintf(L", exchange code=%c, market maker=%ls, price=%f, size=%lld",
                order.exchange_code, order.market_maker, order.price, order.size);
            if (wcslen(order.source) > 0)
                wprintf(L", source=%ls", order.source);
            wprintf(L", count=%d}\n", order.count);
        }
    }
    else if (snapshot_data->event_type == DXF_ET_CANDLE) {
        dxf_candle_t* candle_records = (dxf_candle_t*)snapshot_data->records;
        for (i = 0; i < snapshot_data->records_count; ++i) {
            dxf_candle_t candle = candle_records[i];

            if (i >= RECORDS_PRINT_LIMIT) {
                wprintf(L"   { ... %d records left ...}\n", records_count - i);
                break;
            }

            wprintf(L"time=");
            print_timestamp(candle.time);
            wprintf(L", sequence=%d, count=%f, open=%f, high=%f, low=%f, close=%f, volume=%f, "
                L"VWAP=%f, bidVolume=%f, askVolume=%f}\n",
                candle.sequence, candle.count, candle.open, candle.high,
                candle.low, candle.close, candle.volume, candle.vwap,
                candle.bid_volume, candle.ask_volume);
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
            L"Usage: SnapshotConsoleSample <server address> <event type> <symbol> [order_source]\n"
            L"  <server address> - a DXFeed server address, e.g. demo.dxfeed.com:7300\n"
            L"  <event type> - an event type, one of the following: ORDER, CANDLE\n"
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

    wprintf(L"SnapshotConsoleSample test started.\n");
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
        };
    }
    else {
        if (!dxf_create_order_snapshot(connection, base_symbol, order_source_ptr, 0, &snapshot)) {
            process_last_error();
            dxf_close_connection(connection);
            return -1;
        };
    }

    if (!dxf_attach_snapshot_listener(snapshot, listener, NULL)) {
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
