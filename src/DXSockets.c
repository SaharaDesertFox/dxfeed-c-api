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

#include "DXSockets.h"
#include "DXErrorHandling.h"
#include "DXErrorCodes.h"

/* -------------------------------------------------------------------------- */
/*
 *	Socket error codes
 */
/* -------------------------------------------------------------------------- */

static struct dx_error_code_descr_t g_socket_errors[] = {
    { dx_sec_socket_subsystem_init_failed, "Socket subsystem initialization failed" },
    { dx_sec_socket_subsystem_init_required, "Internal software error" },
    { dx_sec_socket_subsystem_incompatible_version, "Incompatible version of socket subsystem" },
    { dx_sec_connection_gracefully_closed, "Connection gracefully closed by peer" },
    { dx_sec_network_is_down, "Network is down" },
    { dx_sec_blocking_call_in_progress, "Internal software error" },
    { dx_sec_addr_family_not_supported, "Internal software error" },
    { dx_sec_no_sockets_available, "No sockets available" },
    { dx_sec_no_buffer_space_available, "Not enough space in socket buffers" },
    { dx_sec_proto_not_supported, "Internal software error" },
    { dx_sec_socket_type_proto_incompat, "Internal software error" },
    { dx_sec_socket_type_addrfam_incompat, "Internal software error" },
    { dx_sec_addr_already_in_use, "Internal software error" },
    { dx_sec_blocking_call_interrupted, "Internal software error" },
    { dx_sec_nonblocking_oper_pending, "Internal software error" },
    { dx_sec_addr_not_valid, "Internal software error" },
    { dx_sec_connection_refused, "Connection refused" },
    { dx_sec_invalid_ptr_arg, "Internal software error" },
    { dx_sec_invalid_arg, "Internal software error" },
    { dx_sec_sock_already_connected, "Internal software error" },
    { dx_sec_network_is_unreachable, "Network is unreachable" },
    { dx_sec_sock_oper_on_nonsocket, "Internal software error" },
    { dx_sec_connection_timed_out, "Connection timed out" },
    { dx_sec_res_temporarily_unavail, "Internal software error" },
    { dx_sec_permission_denied, "Permission denied" },
    { dx_sec_network_dropped_connection, "Network dropped connection on reset" },
    { dx_sec_socket_not_connected, "Internal software error" },
    { dx_sec_operation_not_supported, "Internal software error" },
    { dx_sec_socket_shutdown, "Internal software error" },
    { dx_sec_message_too_long, "Internal software error" },
    { dx_sec_no_route_to_host, "No route to host" },
    { dx_sec_connection_aborted, "Connection aborted" },
    { dx_sec_connection_reset, "Connection reset" },
    { dx_sec_persistent_temp_error, "Temporary failure in name resolution persists for too long" },
    { dx_sec_unrecoverable_error, "Internal software error" },
    { dx_sec_not_enough_memory, "Not enough memory to complete a socket operation" },
    { dx_sec_no_data_on_host, "Internal software error" },
    { dx_sec_host_not_found, "Host not found" },

    { dx_sec_generic_error, "Unrecognized socket error" },
    
    { ERROR_CODE_FOOTER, ERROR_DESCR_FOOTER }
};

const struct dx_error_code_descr_t* socket_error_roster = g_socket_errors;

/* -------------------------------------------------------------------------- */

enum dx_socket_error_code_t dx_wsa_error_code_to_internal (int wsa_code) {
    switch (wsa_code) {
    case WSANOTINITIALISED:
        return dx_sec_socket_subsystem_init_required;
    case WSAENETDOWN:
        return dx_sec_network_is_down;
    case WSAEINPROGRESS:
        return dx_sec_blocking_call_in_progress;
    case WSAEAFNOSUPPORT:
        return dx_sec_addr_family_not_supported;
    case WSAEMFILE:
        return dx_sec_no_sockets_available;
    case WSAENOBUFS:
        return dx_sec_no_buffer_space_available;
    case WSAEPROTONOSUPPORT:
        return dx_sec_proto_not_supported;
    case WSAEPROTOTYPE:
        return dx_sec_socket_type_proto_incompat;
    case WSAESOCKTNOSUPPORT:
        return dx_sec_socket_type_addrfam_incompat;
    case WSAEADDRINUSE:
        return dx_sec_addr_already_in_use;
    case WSAEINTR:
        return dx_sec_blocking_call_interrupted;
    case WSAEALREADY:
        return dx_sec_nonblocking_oper_pending;
    case WSAEADDRNOTAVAIL:
        return dx_sec_addr_not_valid;
    case WSAECONNREFUSED:
        return dx_sec_connection_refused;
    case WSAEFAULT:
        return dx_sec_invalid_ptr_arg;
    case WSAEINVAL:
        return dx_sec_invalid_arg;
    case WSAEISCONN:
        return dx_sec_sock_already_connected;
    case WSAENETUNREACH:
        return dx_sec_network_is_unreachable;
    case WSAENOTSOCK:
        return dx_sec_sock_oper_on_nonsocket;
    case WSAETIMEDOUT:
        return dx_sec_connection_timed_out;
    case WSAEWOULDBLOCK:
        return dx_sec_res_temporarily_unavail;
    case WSAEACCES:
        return dx_sec_permission_denied;
    case WSAENETRESET:
        return dx_sec_network_dropped_connection;
    case WSAENOTCONN:
        return dx_sec_socket_not_connected;
    case WSAEOPNOTSUPP:
        return dx_sec_operation_not_supported;
    case WSAESHUTDOWN:
        return dx_sec_socket_shutdown;
    case WSAEMSGSIZE:
        return dx_sec_message_too_long;
    case WSAEHOSTUNREACH:
        return dx_sec_no_route_to_host;
    case WSAECONNABORTED:
        return dx_sec_connection_aborted;
    case WSAECONNRESET:
        return dx_sec_connection_reset;
    case WSATRY_AGAIN:
        return dx_sec_persistent_temp_error;
    case WSANO_RECOVERY:
        return dx_sec_unrecoverable_error;
    case WSA_NOT_ENOUGH_MEMORY:
        return dx_sec_not_enough_memory;
    case WSANO_DATA:
        return dx_sec_no_data_on_host;
    case WSAHOST_NOT_FOUND:
        return dx_sec_host_not_found;
    case WSATYPE_NOT_FOUND:
        return dx_sec_socket_type_proto_incompat;
    default:
        return dx_sec_generic_error;    
    }
}

/* -------------------------------------------------------------------------- */
/*
 *	Socket function wrappers
 
 *  Win32 implementation
 */
/* -------------------------------------------------------------------------- */

#ifdef _WIN32

/* ---------------------------------- */
/*
 *	Auxiliary stuff
 */
/* ---------------------------------- */

static bool g_sock_subsystem_initialized = false;

bool dx_init_socket_subsystem () {
    WORD wVersionRequested;
    WSADATA wsaData;
        
    if (g_sock_subsystem_initialized) {
        return true;
    }

    wVersionRequested = MAKEWORD(2, 0);

    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        dx_set_last_error(sc_sockets, dx_sec_socket_subsystem_init_failed);
        
        return false;
    }
    
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 0) {
        dx_set_last_error(sc_sockets, dx_sec_socket_subsystem_incompatible_version);
        
        WSACleanup();
        
        return false;
    }

    return (g_sock_subsystem_initialized = true);
}

/* -------------------------------------------------------------------------- */

bool dx_deinit_socket_subsystem () {
    if (WSACleanup() == SOCKET_ERROR) {
        dx_set_last_error(sc_sockets, (int)dx_wsa_error_code_to_internal(WSAGetLastError()));
        
        return false;
    }
    
    g_sock_subsystem_initialized = false;
    
    return true;
}

/* -------------------------------------------------------------------------- */
/*
 *	DX socket API implementation
 */
/* -------------------------------------------------------------------------- */

dx_socket_t dx_socket (int family, int type, int protocol) {
    dx_socket_t s = INVALID_SOCKET;
    u_long dummy;
    
    if (!dx_init_socket_subsystem()) {
        return INVALID_SOCKET;
    }
    
    if ((s = socket(family, type, protocol)) != INVALID_SOCKET) {
        if (ioctlsocket(s, FIONBIO, &dummy) != INVALID_SOCKET) {
            return s;
        }
        
        dx_close(s);
    }
    
    dx_set_last_error(sc_sockets, (int)dx_wsa_error_code_to_internal(WSAGetLastError()));
    
    return INVALID_SOCKET;
}

/* -------------------------------------------------------------------------- */

bool dx_connect (dx_socket_t s, const struct sockaddr* addr, socklen_t addrlen) {
    int res;

    if ((res = connect(s, addr, addrlen)) == SOCKET_ERROR &&
        (WSAGetLastError() == WSAEWOULDBLOCK)) {
        /* this is the only normal case, since we're using a non-blocking socket */
        int dummy = 0;
        struct fd_set socket_to_wait;
        
        FD_ZERO(&socket_to_wait);
        FD_SET(s, &socket_to_wait);
        
        if (select(dummy, NULL, &socket_to_wait, NULL, NULL) == 1) {
            /* socket was successfully connected */
            
            return true;
        }
    }
    
    if (res != 0) {
        dx_set_last_error(sc_sockets, (int)dx_wsa_error_code_to_internal(WSAGetLastError()));
        
        return false;
    }
    
    return true;
}

/* -------------------------------------------------------------------------- */

int dx_send (dx_socket_t s, const void* buffer, int buflen) {
    int res = send(s, (const char*)buffer, buflen, 0);
    
    if (res == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            return 0;
        }
        
        dx_set_last_error(sc_sockets, (int)dx_wsa_error_code_to_internal(WSAGetLastError()));
        
        return INVALID_DATA_SIZE;
    }
    
    return res;
}

/* -------------------------------------------------------------------------- */

int dx_recv (dx_socket_t s, void* buffer, int buflen) {
    int res = recv(s, (char*)buffer, buflen, 0);

    switch (res) {
    case 0:
        dx_set_last_error(sc_sockets, dx_sec_connection_gracefully_closed);
        break;
    case SOCKET_ERROR:
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            /* no data is queued */
            
            return 0;
        }
        
        dx_set_last_error(sc_sockets, (int)dx_wsa_error_code_to_internal(WSAGetLastError()));        
        break;
    default:
        return res;
    }

    return INVALID_DATA_SIZE;
}

/* -------------------------------------------------------------------------- */

bool dx_close (dx_socket_t s) {
    if (shutdown(s, SD_BOTH) == INVALID_SOCKET) {
        dx_set_last_error(sc_sockets, (int)dx_wsa_error_code_to_internal(WSAGetLastError()));

        return false;
    }
    
    if (closesocket(s) == INVALID_SOCKET) {
        dx_set_last_error(sc_sockets, (int)dx_wsa_error_code_to_internal(WSAGetLastError()));
        
        return false;
    }
    
    if (!dx_deinit_socket_subsystem()) {
        return false;
    }
    
    return true;
}

/* -------------------------------------------------------------------------- */

const size_t dx_name_resolution_attempt_count = 5;

bool dx_getaddrinfo (const char* nodename, const char* servname,
                     const struct addrinfo* hints, struct addrinfo** res) {
    int funres = 0;
    size_t iter_count = 0;
    
    if (!dx_init_socket_subsystem()) {
        return false;
    }
    
    for (; iter_count < dx_name_resolution_attempt_count; ++iter_count) {
        funres = getaddrinfo(nodename, servname, hints, res);
        
        if (funres == WSATRY_AGAIN) {
            continue;
        }
        
        break;
    }
    
    if (funres != 0) {
        dx_set_last_error(sc_sockets, (int)dx_wsa_error_code_to_internal(funres));
        
        return false;
    }
    
    return true;
}

/* -------------------------------------------------------------------------- */

void dx_freeaddrinfo (struct addrinfo* res) {
    freeaddrinfo(res);
}

#endif // _WIN32