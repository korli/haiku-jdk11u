/*
 * Copyright (c) 2003, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

//#define USE_ERROR
//#define USE_TRACE

#include "PLATFORM_API_HaikuOS_Utils.h"

extern "C" {
#include "Ports.h"
}

#if USE_PORTS == TRUE

////////////////////////////////////////////////////////////////////////////////
// functions from Port.h

INT32 PORT_GetPortMixerCount() {
    int count = 0;
    TRACE1("<<PORT_GetPortMixerCount = %d\n", count);
    return count;
}

INT32 PORT_GetPortMixerDescription(INT32 mixerIndex, PortMixerDescription* mixerDescription) {
    return FALSE;
}

void* PORT_Open(INT32 mixerIndex) {
    TRACE1("\n>>PORT_Open (mixerIndex=%d)\n", (int)mixerIndex);
    return NULL;
}


void PORT_Close(void* id) {
    TRACE1(">>PORT_Close %p\n", id);
}

INT32 PORT_GetPortCount(void* id) {
    int count = 0;
    TRACE1("<<PORT_GetPortCount = %d\n", count);
    return count;
}

INT32 PORT_GetPortType(void* id, INT32 portIndex) {
	int ret = 0
    TRACE2("<<PORT_GetPortType (portIndex=%d) = %d\n", portIndex, ret);
    return ret;
}

INT32 PORT_GetPortName(void* id, INT32 portIndex, char* name, INT32 len) {
    TRACE2("<<PORT_GetPortName (portIndex = %d) = %s\n", portIndex, NULL);
    return FALSE;
}

void PORT_GetControls(void* id, INT32 portIndex, PortControlCreator* creator) {
    TRACE1(">>PORT_GetControls (portIndex = %d)\n", portIndex);
    TRACE1("<<PORT_GetControls (portIndex = %d)\n", portIndex);
}

INT32 PORT_GetIntValue(void* controlIDV) {
    INT32 result = 0;

    TRACE1("<<PORT_GetIntValue = %d\n", result);
    return result;
}

void PORT_SetIntValue(void* controlIDV, INT32 value) {
    TRACE1("> PORT_SetIntValue = %d\n", value);
}

float PORT_GetFloatValue(void* controlIDV) {
    float result = 0;
    TRACE1("<<PORT_GetFloatValue = %f\n", result);
    return result;
}

void PORT_SetFloatValue(void* controlIDV, float value) {
    TRACE1("> PORT_SetFloatValue = %f\n", value);
}

#endif // USE_PORTS
