/*
 * Copyright (c) 2014, Oracle and/or its affiliates. All rights reserved.
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

#include <jlong.h>
#include <jni.h>

#include "sun_hawt_HaikuDropTargetContextPeer.h"

#include <Message.h>


extern "C" {

/*
 * Class:     sun_hawt_HaikuDropTargetContextPeer
 * Method:    nativeFreeMessage
 * Signature: (J)V
 */
JNIEXPORT void JNICALL
Java_sun_hawt_HaikuDropTargetContextPeer_nativeFreeMessage(JNIEnv* env,
	jobject thiz, jlong nativeMessage)
{
	BMessage* message = (BMessage*)jlong_to_ptr(nativeMessage);
	delete message;
}


/*
 * Class:     sun_hawt_HaikuDropTargetContextPeer
 * Method:    nativeGetData
 * Signature: (JLjava/lang/string;)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_sun_hawt_HaikuDropTargetContextPeer_nativeGetData(JNIEnv* env, jobject thiz,
    jlong nativeMessage, jstring format)
{
	BMessage* message = (BMessage*)jlong_to_ptr(nativeMessage);

	const char* mimeType = env->GetStringUTFChars(format, NULL);
	if (mimeType == NULL)
		return NULL;

	ssize_t length;
	const void* data;
	status_t result = message->FindData(mimeType, B_MIME_TYPE, 0, &data, &length);
	env->ReleaseStringUTFChars(format, mimeType);

	if (result != B_OK)
		return NULL;

	jbyteArray bytes = env->NewByteArray(length);
	if (bytes == NULL)
		return NULL;

	env->SetByteArrayRegion(bytes, 0, length, (jbyte*)data);
	return bytes;	
}

}
