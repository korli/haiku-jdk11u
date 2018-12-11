/*
 * Copyright (c) 2012, Oracle and/or its affiliates. All rights reserved.
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

#include "jni.h"
#include "jni_util.h"
#include "jvm.h"
#include "jlong.h"
#include "nio_util.h"

#include "sun_nio_ch_HQueue.h"

#include <strings.h>
#include <sys/types.h>
#include <sys/time.h>
#include <OS.h>

JNIEXPORT jint JNICALL
Java_sun_nio_ch_HQueue_heventSize(JNIEnv* env, jclass this)
{
    return sizeof(struct object_wait_info);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_HQueue_objectOffset(JNIEnv* env, jclass this)
{
    return offsetof(struct object_wait_info, object);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_HQueue_typeOffset(JNIEnv* env, jclass this)
{
    return offsetof(struct object_wait_info, type);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_HQueue_eventsOffset(JNIEnv* env, jclass this)
{
    return offsetof(struct object_wait_info, events);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_HQueue_heventPoll(JNIEnv *env, jclass c,
                                  jlong address, jint nevents)
{
    struct object_wait_info *events = jlong_to_ptr(address);
    int res;

    RESTARTABLE(wait_for_objects(events, nevents), res);
    if (res < 0) {
        JNU_ThrowIOExceptionWithLastError(env, "wait_for_objects failed");
    }
    return res;
}
