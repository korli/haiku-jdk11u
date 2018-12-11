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

#include "AwtApplication.h"

#include <Clipboard.h>
#include <FilePanel.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageFilter.h>
#include <Path.h>

#include "SharedApplication.h"

#include "HaikuToolkit.h"
#include "Utilities.h"


ApplicationFilter::ApplicationFilter()
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
	fEnv(NULL)
{
	fEnv = AttachToJVM(jvm);
}


filter_result
ApplicationFilter::Filter(BMessage* message, BHandler** target)
{
	switch (message->what) {
		case kMenuMessage: {
			jobject peer;
			status_t result = message->FindPointer("peer", (void**)&peer);
			assert(result == B_OK && peer != NULL);

			jint mods = ConvertInputModifiersToJava(modifiers());

			int64 when = message->GetInt64("when", 0);

			bool check = false;
			if (message->FindBool("checkbox", &check) == B_OK) {
				BMenuItem* item = NULL;
				status_t found = message->FindPointer("source", (void**)&item);
				assert(found == B_OK && item != NULL);
				check = !item->IsMarked();
				item->SetMarked(check);
			}

			fEnv->CallVoidMethod(peer, handleActionMethod, (jlong)(when / 1000),
				mods, check);
			break;
		}
		case kFileMessage:
		case B_CANCEL:
			_HandleFileMessage(message);
			break;
		case B_CLIPBOARD_CHANGED: {
			jclassLocal clipboardClazz(fEnv, fEnv->FindClass("sun/hawt/HaikuClipboard"));
			fEnv->CallStaticVoidMethod(clipboardClazz, clipboardChangedMethod);
			break;
		}
	}
	return B_DISPATCH_MESSAGE;
}


void
ApplicationFilter::_HandleFileMessage(BMessage* msg)
{
	// We might as well free the and ref filter now since all the interesting
	// stuff is in the message
	BFilePanel* panel = NULL;
	status_t found = msg->FindPointer("panel", (void**)&panel);
	assert(found == B_OK && panel != NULL);

	if (msg->what == B_CANCEL) {
		// Only delete the panel on cancel, which is sent even after a
		// successful open or save.
		delete panel;

		BRefFilter* filter = NULL;
		if (msg->FindPointer("filter", (void**)&filter) == B_OK)
			delete filter;
	}

	jobject peer;
	found = msg->FindPointer("peer", (void**)&peer);
	assert(found == B_OK && peer != NULL);

	jobjectArrayLocal result(fEnv, NULL);
	if (msg->what == kFileMessage) {
		bool save = false;
		found = msg->FindBool("save", &save);
		assert(found == B_OK);

		if (save) {
			result = _HandleSaveMessage(msg);
		} else {
			result = _HandleOpenMessage(msg);
		}
	}

	fEnv->CallVoidMethod(peer, doneMethod, (jobjectArray)result);

	if (msg->what == B_CANCEL)
		fEnv->DeleteWeakGlobalRef(peer);
}


jobjectArray
ApplicationFilter::_HandleOpenMessage(BMessage* msg)
{
	// Files opened, we get some number of refs (hopefully)
	type_code typeFound;
	int32 count;
	if (msg->GetInfo("refs", &typeFound, &count) != B_OK)
		return NULL;

	if (typeFound != B_REF_TYPE || count < 1)
		return NULL;

	jclassLocal stringClazz(fEnv, fEnv->FindClass("java/lang/String"));
	jobjectArray result = fEnv->NewObjectArray(count, stringClazz, NULL);

	assert(result != NULL);

	entry_ref ref;
	for (int i = 0; i < count; i++) {
		msg->FindRef("refs", i, &ref);
		BPath path = BPath(&ref);
		jstringLocal file(fEnv, fEnv->NewStringUTF(path.Path()));
		assert(file != NULL);
		fEnv->SetObjectArrayElement(result, i, file);
	}

	return result;
}

jobjectArray
ApplicationFilter::_HandleSaveMessage(BMessage* msg)
{
	// File saved, we get a dir ref in "directory" and a leaf
	// string in "name".
	entry_ref dir;
	const char* leaf;
	if (msg->FindRef("directory", &dir) != B_OK
			|| msg->FindString("name", &leaf) != B_OK)
		return NULL;

	BPath path = BPath(&dir);
	path.Append(leaf);

	jclassLocal stringClazz(fEnv, fEnv->FindClass("java/lang/String"));
	jobjectArray result = fEnv->NewObjectArray(1, stringClazz, NULL);

	assert(result != NULL);

	jstringLocal file(fEnv, fEnv->NewStringUTF(path.Path()));
	assert(file != NULL);

	fEnv->SetObjectArrayElement(result, 0, file);
	return result;
}
