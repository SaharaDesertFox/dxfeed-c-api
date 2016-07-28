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

/*
*	Contains the functionality for managing the memory required to store
*  the string data
*/

#ifndef STRING_ARRAY_H_INCLUDED
#define STRING_ARRAY_H_INCLUDED

#include "DXTypes.h"
#include "PrimitiveTypes.h"

typedef struct {
    dxf_const_string_t* elements;
    int size;
    int capacity;
} dx_string_array_t, *dx_string_array_ptr_t;

bool dx_string_array_add(dx_string_array_t* string_array, dxf_const_string_t str);
void dx_string_array_free(dx_string_array_t* string_array);

#endif /* STRING_ARRAY_H_INCLUDED */