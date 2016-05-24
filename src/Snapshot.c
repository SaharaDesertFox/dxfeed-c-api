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

#include "ConnectionContextData.h"
#include "DXAlgorithms.h"
#include "DXErrorCodes.h"
#include "DXErrorHandling.h"
#include "DXThreads.h"
#include "EventSubscription.h"
#include "Logger.h"
#include "Snapshot.h"

/* -------------------------------------------------------------------------- */
/*
*	Internal data structures and defines
*/
/* -------------------------------------------------------------------------- */

typedef enum {
    dx_status_unknown = 0,
    dx_status_begin,
    dx_status_full,
    dx_status_pending
} dx_snapshot_status_t;

typedef struct {
    dxf_snapshot_listener_t listener;
    void* user_data;
} dx_snapshot_listener_context_t;

typedef struct {
    dx_snapshot_listener_context_t* elements;
    int size;
    int capacity;
} dx_snapshot_listener_array_t;

typedef struct {
    dxf_event_data_t* elements;
    int size;
    int capacity;
} dx_snapshot_records_array_t;

typedef struct {
    dxf_ulong_t key;
    dxf_subscription_t subscription;
    dx_record_id_t record_id;
    dx_event_id_t event_id;
    int event_type;
    dxf_string_t order_source;
    dxf_string_t symbol;
    dx_snapshot_status_t status;

    dx_snapshot_records_array_t records;
    dx_snapshot_listener_array_t listeners;

    void* sscc;
} dx_snapshot_data_t, *dx_snapshot_data_ptr_t;

//TODO: possible need to map (key(s)->value) implementation
//      and/or improve search algorithms
typedef struct {
    dx_snapshot_data_ptr_t* elements;
    int size;
    int capacity;
} dx_snapshots_data_array_t;

const dxf_snapshot_t dx_invalid_snapshot = (dxf_snapshot_t)NULL;

#define SYMBOL_COUNT 1

/* -------------------------------------------------------------------------- */
/*
*	Event subscription connection context
*/
/* -------------------------------------------------------------------------- */

typedef struct {
    dxf_connection_t connection;
    dx_mutex_t guard;
    dx_snapshots_data_array_t snapshots_array;
    int fields_flags;
} dx_snapshot_subscription_connection_context_t;

#define GUARD_FIELD_FLAG    (0x1)

#define CTX(context) \
    ((dx_snapshot_subscription_connection_context_t*)context)

/* -------------------------------------------------------------------------- */

bool dx_clear_snapshot_subscription_connection_context(dx_snapshot_subscription_connection_context_t* context);
bool dx_free_snapshot_data(dx_snapshot_data_t* snapshot_data);

DX_CONNECTION_SUBSYS_INIT_PROTO(dx_ccs_snapshot_subscription) {
    dx_snapshot_subscription_connection_context_t* context = NULL;

    CHECKED_CALL_2(dx_validate_connection_handle, connection, true);

    context = dx_calloc(1, sizeof(dx_snapshot_subscription_connection_context_t));

    if (context == NULL) {
        return false;
    }

    context->connection = connection;

    if (!dx_mutex_create(&(context->guard))) {
        dx_clear_snapshot_subscription_connection_context(context);

        return false;
    }

    context->fields_flags |= GUARD_FIELD_FLAG;

    if (!dx_set_subsystem_data(connection, dx_ccs_snapshot_subscription, context)) {
        dx_clear_snapshot_subscription_connection_context(context);

        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */

DX_CONNECTION_SUBSYS_DEINIT_PROTO(dx_ccs_snapshot_subscription) {
    bool res = true;
    dx_snapshot_subscription_connection_context_t* context = dx_get_subsystem_data(connection, dx_ccs_snapshot_subscription, &res);

    if (context == NULL) {
        return res;
    }

    return dx_clear_snapshot_subscription_connection_context(context);
}

/* -------------------------------------------------------------------------- */

DX_CONNECTION_SUBSYS_CHECK_PROTO(dx_ccs_snapshot_subscription) {
    return true;
}

/* -------------------------------------------------------------------------- */

bool dx_clear_snapshot_subscription_connection_context(dx_snapshot_subscription_connection_context_t* context) {
    bool res = true;
    int i = 0;

    for (; i < context->snapshots_array.size; ++i) {
        res = dx_free_snapshot_data((dx_snapshot_data_t*)context->snapshots_array.elements[i]) && res;
    }

    if (IS_FLAG_SET(context->fields_flags, GUARD_FIELD_FLAG)) {
        res = dx_mutex_destroy(&(context->guard)) && res;
    }

    if (context->snapshots_array.elements != NULL) {
        dx_free(context->snapshots_array.elements);
    }

    dx_free(context);

    return res;
}

/* -------------------------------------------------------------------------- */
/*
*	Helpers functions implementation
*/
/* -------------------------------------------------------------------------- */

bool dx_snapshot_add_event_records(dx_snapshot_data_ptr_t snapshot_data, const dxf_event_data_t* data, int data_count) {
    int i;

    for (i = 0; i < data_count; ++i) {
        bool failed = false;
        dxf_event_data_t obj = NULL;
        dx_event_copy_function_t clone_event = dx_get_event_copy_function(snapshot_data->event_id);
        dxf_bool_t res = false;
        if (clone_event == NULL) {
            return false;
        }
        if (!clone_event(&data[i], &obj)) {
            return false;
        }
        DX_ARRAY_INSERT(snapshot_data->records, dxf_event_data_t, obj, snapshot_data->records.size, dx_capacity_manager_halfer, failed);
        if (failed) {
            return dx_set_error_code(dx_mec_insufficient_memory);
        }
    }
    return true;
}

bool dx_snapshot_clear_records_array(dx_snapshot_records_array_t* records, dx_event_id_t event_id) {
    int i;
    dx_event_free_function_t free_event = dx_get_event_free_function(event_id);

    if (free_event == NULL) {
        return false;
    }

    for (i = 0; i < records->size; i++) {
        free_event(records->elements[i]);
    }
    dx_free(records->elements);
    records->elements = NULL;
    records->size = 0;
    records->capacity = 0;
    return true;
}

bool dx_snapshot_call_listeners(dx_snapshot_data_ptr_t snapshot_data) {
    int cur_listener_index = 0;

    for (; cur_listener_index < snapshot_data->listeners.size; ++cur_listener_index) {
        dx_snapshot_listener_context_t* listener_context = snapshot_data->listeners.elements + cur_listener_index;
        bool failed = false;

        dxf_snapshot_data_t callback_data;
        callback_data.record_id = snapshot_data->record_id;
        callback_data.event_id = snapshot_data->event_id;
        callback_data.event_type = snapshot_data->event_type;
        callback_data.symbol = dx_create_string_src(snapshot_data->symbol);
        callback_data.records_count = snapshot_data->records.size;
        callback_data.records = (const dxf_event_data_t*)snapshot_data->records.elements;
        listener_context->listener(&callback_data, listener_context->user_data);
        dx_free(callback_data.symbol);
    }
    return true;
}

void event_listener(int event_type, dxf_const_string_t symbol_name, const dxf_event_data_t* data,
                    dxf_event_flags_t flags, int data_count, void* user_data) {
    int i;
    dx_snapshot_subscription_connection_context_t* context = CTX(user_data);

    /* locking a guard mutex */
    dx_mutex_lock(&(context->guard));

    for (i = 0; i < context->snapshots_array.size; i++) {
        dx_snapshot_data_ptr_t snapshot_data = context->snapshots_array.elements[i];
        if (snapshot_data->event_type != event_type
            || dx_compare_strings(snapshot_data->symbol, symbol_name) != 0) {
            continue;
        }

        //check status
        if (snapshot_data->status == dx_status_unknown && (!IS_FLAG_SET(flags, dxf_ef_snapshot_begin))) {
            continue;
        }
        if (IS_FLAG_SET(flags, dxf_ef_snapshot_begin)) {
            snapshot_data->status = dx_status_begin;
            dx_snapshot_clear_records_array(&(snapshot_data->records), snapshot_data->event_id);
            dx_snapshot_add_event_records(snapshot_data, data, data_count);
            continue;
        }
        if (IS_FLAG_SET(flags, dxf_ef_snapshot_end) /* TODO: time check */) {
            dx_snapshot_add_event_records(snapshot_data, data, data_count);
            snapshot_data->status = dx_status_full;
            dx_snapshot_call_listeners(snapshot_data);
            continue;
        }
        if (IS_FLAG_SET(flags, dxf_ef_tx_pending)) {
            //TODO: update record
            snapshot_data->status = dx_status_pending;
            continue;
        }

        if (snapshot_data->status == dx_status_begin) {
            dx_snapshot_add_event_records(snapshot_data, data, data_count);
            continue;
        }
        else if (snapshot_data->status == dx_status_pending) {
            //TODO: apply update
            continue;
        }
        else if (snapshot_data->status == dx_status_full) {
            //TODO: apply one-row-update
            snapshot_data->status = dx_status_full;
            dx_snapshot_call_listeners(snapshot_data);
            continue;
        }

        /* unknown state */
    }

    /* unlocking a guard mutex */
    dx_mutex_unlock(&(context->guard));

}

dxf_ulong_t dx_new_snapshot_key(dx_record_id_t record_id, dxf_const_string_t symbol,
    dxf_const_string_t order_source) {
    dxf_int_t symbol_hash = dx_symbol_name_hasher(symbol);
    dxf_int_t order_source_hash = (order_source == NULL ? 0 : dx_symbol_name_hasher(order_source));
    return ((dxf_ulong_t)record_id << 56) | ((dxf_ulong_t)symbol_hash << 24) | (order_source_hash & 0xFFFFFF);
}

int dx_snapshot_comparator(dx_snapshot_data_ptr_t s1, dx_snapshot_data_ptr_t s2) {
    return DX_NUMERIC_COMPARATOR(s1->key, s2->key);
}

//TODO: repeated code
void dx_clear_snapshot_listener_array(dx_snapshot_listener_array_t* listeners) {
    dx_free(listeners->elements);

    listeners->elements = NULL;
    listeners->size = 0;
    listeners->capacity = 0;
}

//TODO: repeated code
int dx_snapshot_listener_comparator(dx_snapshot_listener_context_t e1, dx_snapshot_listener_context_t e2) {
    return DX_NUMERIC_COMPARATOR(e1.listener, e2.listener);
}

//TODO: repeated code
int dx_find_snapshot_listener_in_array(dx_snapshot_listener_array_t* listeners, dxf_snapshot_listener_t listener, OUT bool* found) {
    int listener_index;
    dx_snapshot_listener_context_t listener_context = { listener, NULL };

    DX_ARRAY_SEARCH(listeners->elements, 0, listeners->size, listener_context, dx_snapshot_listener_comparator, false, *found, listener_index);

    return listener_index;
}

bool dx_free_snapshot_data(dx_snapshot_data_t* snapshot_data) {
    if (snapshot_data == NULL) {
        return false;
    }

    //remove listeners
    dx_clear_snapshot_listener_array(&(snapshot_data->listeners));

    //remove records
    dx_snapshot_clear_records_array(&(snapshot_data->records), snapshot_data->event_id);

    if (snapshot_data->symbol != NULL)
        dx_free(snapshot_data->symbol);
    if (snapshot_data->order_source != NULL)
        dx_free(snapshot_data->order_source);

    dx_free(snapshot_data);
    return true;
}

/* -------------------------------------------------------------------------- */
/*
*	Subscription functions implementation
*/
/* -------------------------------------------------------------------------- */

dxf_snapshot_t dx_create_snapshot(dxf_connection_t connection, 
                                  dxf_subscription_t subscription,
                                  dx_event_id_t event_id,
                                  dx_record_id_t record_id, 
                                  dxf_const_string_t symbol, 
                                  dxf_const_string_t order_source) {
    dx_snapshot_subscription_connection_context_t* context = NULL;
    dx_snapshot_data_t *snapshot_data = NULL;
    bool failed = false;
    bool found = false;
    bool res = false;
    int position = 0;
    int event_types;

    if (!dx_validate_connection_handle(connection, false)) {
        return dx_invalid_snapshot;
    }

    context = dx_get_subsystem_data(connection, dx_ccs_snapshot_subscription, &res);
    if (context == NULL) {
        if (res) {
            dx_set_error_code(dx_cec_connection_context_not_initialized);
        }

        return dx_invalid_snapshot;
    }

    if (!dx_get_event_subscription_event_types(subscription, &event_types)) {
        return dx_invalid_snapshot;
    }

    snapshot_data = dx_calloc(1, sizeof(dx_snapshot_data_t));
    if (snapshot_data == NULL) {
        return dx_invalid_snapshot;
    }
    snapshot_data->key = dx_new_snapshot_key(record_id, symbol, order_source);
    snapshot_data->record_id = record_id;
    snapshot_data->event_id = event_id;
    snapshot_data->event_type = event_types;
    snapshot_data->symbol = dx_create_string_src(symbol);
    if (order_source != NULL)
        snapshot_data->order_source = dx_create_string_src(order_source);
    snapshot_data->status = dx_status_unknown;
    snapshot_data->sscc = context;
    snapshot_data->subscription = subscription;

    if (!dx_add_listener(snapshot_data->subscription, event_listener, (void*)context)) {
        dx_free_snapshot_data(snapshot_data);
        return dx_invalid_snapshot;
    }

    if (!dx_mutex_lock(&(context->guard))) {
        dx_free_snapshot_data(snapshot_data);
        return dx_invalid_snapshot;
    }

    if (context->snapshots_array.size > 0) {
        DX_ARRAY_SEARCH(context->snapshots_array.elements, 0, context->snapshots_array.size, snapshot_data, dx_snapshot_comparator, false, found, position);
        if (found) {
            dx_free_snapshot_data(snapshot_data);
            dx_set_error_code(dx_ssec_snapshot_exist);
            dx_mutex_unlock(&(context->guard));
            return dx_invalid_snapshot;
        }
    }

    //add snapshot to array
    DX_ARRAY_INSERT(context->snapshots_array, dx_snapshot_data_ptr_t, snapshot_data, position, dx_capacity_manager_halfer, failed);
    if (failed) {
        dx_free_snapshot_data(snapshot_data);
        snapshot_data = dx_invalid_snapshot;
    }

    return (dx_mutex_unlock(&(context->guard)) ? (dxf_snapshot_t)snapshot_data : dx_invalid_snapshot);
}

bool dx_close_snapshot(dxf_snapshot_t snapshot) {
    dx_snapshot_subscription_connection_context_t* context = NULL;
    dx_snapshot_data_ptr_t snapshot_data = NULL;
    bool found = false;
    bool failed = false;
    int position = 0;

    if (snapshot == dx_invalid_snapshot) {
        return dx_set_error_code(dx_ssec_invalid_snapshot_id);
    }

    snapshot_data = (dx_snapshot_data_ptr_t)snapshot;
    context = CTX(snapshot_data->sscc);

    /* locking a guard mutex */
    CHECKED_CALL(dx_mutex_lock, &(context->guard));

    //remove item from snapshots_array
    DX_ARRAY_SEARCH(context->snapshots_array.elements, 0, context->snapshots_array.size, snapshot_data, dx_snapshot_comparator, false, found, position);
    if (found) {
        DX_ARRAY_DELETE(context->snapshots_array, dx_snapshot_data_ptr_t, position, dx_capacity_manager_halfer, failed);
        if (failed) {
            dx_set_error_code(dx_mec_insufficient_memory);
        }
        return !failed && dx_free_snapshot_data(snapshot_data);
    }
    else {
        dx_mutex_unlock(&(context->guard));
        return dx_set_error_code(dx_ssec_invalid_snapshot_id);
    }
}

bool dx_add_snapshot_listener(dxf_snapshot_t snapshot, dxf_snapshot_listener_t listener, void* user_data) {
    dx_snapshot_subscription_connection_context_t* context = NULL;
    dx_snapshot_data_ptr_t snapshot_data = (dx_snapshot_data_ptr_t)snapshot;
    int listener_index;
    bool failed;
    bool found = false;

    if (snapshot == dx_invalid_snapshot) {
        return dx_set_error_code(dx_ssec_invalid_snapshot_id);
    }

    if (listener == NULL) {
        return dx_set_error_code(dx_ssec_invalid_listener);
    }

    context = CTX(snapshot_data->sscc);
    listener_index = dx_find_snapshot_listener_in_array(&(snapshot_data->listeners), listener, &found);

    if (found) {
        /* listener is already added */ 
        return true;
    }

    dx_logging_verbose_info(L"Add snapshot listener: %d", listener_index);

    /* a guard mutex is required to protect the internal containers
    from the secondary data retriever threads */
    CHECKED_CALL(dx_mutex_lock, &(context->guard));

    {
        dx_snapshot_listener_context_t listener_context = { listener, user_data };
        DX_ARRAY_INSERT(snapshot_data->listeners, dx_snapshot_listener_context_t, listener_context, listener_index, dx_capacity_manager_halfer, failed);
    }

    return dx_mutex_unlock(&(context->guard)) && !failed;
}

bool dx_remove_snapshot_listener(dxf_snapshot_t snapshot, dxf_snapshot_listener_t listener) {
    dx_snapshot_subscription_connection_context_t* context = NULL;
    dx_snapshot_data_ptr_t snapshot_data = (dx_snapshot_data_ptr_t)snapshot;
    int listener_index;
    bool failed;
    bool found = false;

    if (snapshot == dx_invalid_snapshot) {
        return dx_set_error_code(dx_ssec_invalid_snapshot_id);
    }

    if (listener == NULL) {
        return dx_set_error_code(dx_ssec_invalid_listener);
    }

    context = CTX(snapshot_data->sscc);
    listener_index = dx_find_snapshot_listener_in_array(&(snapshot_data->listeners), listener, &found);

    if (!found) {
        /* listener isn't subscribed */
        return true;
    }

    dx_logging_verbose_info(L"Remove snapshot listener: %d", listener_index);

    /* a guard mutex is required to protect the internal containers
    from the secondary data retriever threads */
    CHECKED_CALL(dx_mutex_lock, &(context->guard));

    DX_ARRAY_DELETE(snapshot_data->listeners, dx_snapshot_listener_context_t, listener_index, dx_capacity_manager_halfer, failed);

    return dx_mutex_unlock(&(context->guard)) && !failed;
}

bool dx_get_snapshot_subscription(dxf_snapshot_t snapshot, OUT dxf_subscription_t *subscription) {
    if (snapshot == dx_invalid_snapshot) {
        return dx_set_error_code(dx_ssec_invalid_snapshot_id);
    }
    *subscription = ((dx_snapshot_data_t*)snapshot)->subscription;
    return true;
}
