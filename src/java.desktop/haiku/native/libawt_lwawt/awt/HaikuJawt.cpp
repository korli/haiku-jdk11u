/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
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

#include <jawt.h>
#include <jawt_md.h>

#include "ContentView.h"
#include "Utilities.h"

extern "C" {


DECLARE_JAVA_CLASS(awtComponent, "java/awt/Component");
DECLARE_JAVA_CLASS(lwComponentPeer, "sun/lwawt/LWComponentPeer");
DECLARE_JAVA_CLASS(lwWindowPeer, "sun/lwawt/LWWindowPeer");
DECLARE_JAVA_CLASS(platformWindow, "sun/hawt/HaikuPlatformWindow");


JNIEXPORT JAWT_DrawingSurfaceInfo* JNICALL
awt_DrawingSurface_GetDrawingSurfaceInfo(JAWT_DrawingSurface* ds)
{
    JAWT_DrawingSurfaceInfo* dsi = (JAWT_DrawingSurfaceInfo*)malloc(sizeof(JAWT_DrawingSurfaceInfo));

    JNIEnv *env = ds->env;
    jobject target = ds->target;

    DECLARE_JAVA_FIELD(peerField, awtComponent, "peer", "Ljava/awt/peer/ComponentPeer;");
    jobject peer = env->GetObjectField(target, peerField);

    DECLARE_JAVA_FIELD(windowPeerField, lwComponentPeer, "windowPeer", "Lsun/lwawt/LWWindowPeer");
    jobject windowPeer = env->GetObjectField(peer, windowPeerField);

    DECLARE_JAVA_FIELD(platformWindowField, lwWindowPeer, "platformWindow", "Lsun/lwawt/PlatformWindow");
    jobject platformWindow = env->GetObjectField(windowPeer, platformWindowField);

    DECLARE_OBJECT_JAVA_METHOD(getView, platformWindow, "getView", "()J");
    jlong viewPtr = env->CallLongMethod(platformWindow, getView);

    ContentView* contentView = (ContentView*)jlong_to_ptr(viewPtr);

    JAWT_HaikuDrawingSurfaceInfo* platformInfo =
        (JAWT_HaikuDrawingSurfaceInfo*)malloc(sizeof(JAWT_HaikuDrawingSurfaceInfo));
    platformInfo->contentView = contentView;

    dsi->platformInfo = platformInfo;
    dsi->ds = ds;

    DECLARE_JAVA_FIELD(xField, awtComponent, "x", "I");
    DECLARE_JAVA_FIELD(yField, awtComponent, "y", "I");
    DECLARE_JAVA_FIELD(widthField, awtComponent, "width", "I");
    DECLARE_JAVA_FIELD(heightField, awtComponent, "height", "I");

    dsi->bounds.x = env->GetIntField(target, xField);
    dsi->bounds.y = env->GetIntField(target, yField);
    dsi->bounds.width = env->GetIntField(target, widthField);
    dsi->bounds.height = env->GetIntField(target, heightField);

    dsi->clipSize = 1;
    dsi->clip = &(dsi->bounds);

    return dsi;
}

JNIEXPORT jint JNICALL awt_DrawingSurface_Lock(JAWT_DrawingSurface* ds)
{
    // TODO: implement
    return 0;
}

JNIEXPORT void JNICALL awt_DrawingSurface_Unlock(JAWT_DrawingSurface* ds)
{
    // TODO: implement
}

JNIEXPORT void JNICALL
awt_DrawingSurface_FreeDrawingSurfaceInfo(JAWT_DrawingSurfaceInfo* dsi)
{
    free(dsi);
}


JNIEXPORT void JNICALL awt_FreeDrawingSurface(JAWT_DrawingSurface* ds)
{
    ds->env->DeleteGlobalRef(ds->target);
    free(ds);
}

JNIEXPORT JAWT_DrawingSurface* JNICALL
awt_GetDrawingSurface(JNIEnv* env, jobject target)
{
    JAWT_DrawingSurface* ds = (JAWT_DrawingSurface*)malloc(sizeof(JAWT_DrawingSurface));

    jclass awtComponent = env->FindClass("java/awt/Component");
    if (!env->IsInstanceOf(target, awtComponent)) {
        return NULL;
    }

    ds->env = env;
    ds->target = env->NewGlobalRef(target);
    ds->Lock = awt_DrawingSurface_Lock;
    ds->GetDrawingSurfaceInfo = awt_DrawingSurface_GetDrawingSurfaceInfo;
    ds->FreeDrawingSurfaceInfo = awt_DrawingSurface_FreeDrawingSurfaceInfo;
    ds->Unlock = awt_DrawingSurface_Unlock;

    return ds;
}

JNIEXPORT void JNICALL awt_Lock(JNIEnv* env)
{
    // TODO: implement
}

JNIEXPORT void JNICALL awt_Unlock(JNIEnv* env)
{
    // TODO: implement
}

JNIEXPORT jobject JNICALL awt_GetComponent(JNIEnv* env, void* platformInfo)
{
    // TODO: implement
    return NULL;
}

/*
 * Get the AWT native structure.  This function returns JNI_FALSE if
 * an error occurs.
 */
JNIEXPORT jboolean JNICALL JAWT_GetAWT(JNIEnv* env, JAWT* awt)
{
    if (awt == NULL) {
        return JNI_FALSE;
    }

    if (awt->version != JAWT_VERSION_1_3
        && awt->version != JAWT_VERSION_1_4
        && awt->version != JAWT_VERSION_1_7) {
        return JNI_FALSE;
    }

    awt->GetDrawingSurface = awt_GetDrawingSurface;
    awt->FreeDrawingSurface = awt_FreeDrawingSurface;
    if (awt->version >= JAWT_VERSION_1_4) {
        awt->Lock = awt_Lock;
        awt->Unlock = awt_Unlock;
        awt->GetComponent = awt_GetComponent;
    }

    return JNI_TRUE;
}


}
