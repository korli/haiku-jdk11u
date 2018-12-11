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

#include "ContentView.h"

#include <String.h>
#include <Window.h>

#include "java_awt_dnd_DnDConstants.h"
#include "java_awt_event_KeyEvent.h"
#include "java_awt_event_MouseEvent.h"
#include "java_awt_event_MouseWheelEvent.h"

#include "HaikuPlatformWindow.h"


ContentView::ContentView(jobject platformWindow)
	:
	BView(BRect(0, 0, 0, 0), NULL, B_FOLLOW_ALL,
		B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
	fDrawable(this),
	fPlatformWindow(platformWindow),
	fDropTargetComponent(NULL),
	fDropTargetContext(NULL),
	fDragSourceContext(NULL)
{
	get_mouse(&fPreviousPoint, &fPreviousButtons);
}


void
ContentView::StartDrag(BMessage* message, jobject dragSource)
{
	fDragSourceContext = dragSource;

	// TODO use the provided image instead of this rectangle
	BPoint mouse;
	get_mouse(&mouse, NULL);
	ConvertFromScreen(&mouse);
	BRect rect(mouse.x - 64, mouse.y - 64, mouse.x + 63, mouse.y + 63);

	DragMessage(message, rect, this);
}


void
ContentView::Draw(BRect updateRect)
{
	DeferredDraw(updateRect);
}


void
ContentView::KeyDown(const char* bytes, int32 numBytes)
{
	_HandleKeyEvent(Window()->CurrentMessage());
}


void
ContentView::KeyUp(const char* bytes, int32 numBytes)
{
	_HandleKeyEvent(Window()->CurrentMessage());
}


void
ContentView::MakeFocus(bool focused)
{
	BView::MakeFocus(focused);
}


void
ContentView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_UNMAPPED_KEY_DOWN:
		case B_UNMAPPED_KEY_UP:
			_HandleKeyEvent(message);
			break;
		case B_MOUSE_WHEEL_CHANGED:
			_HandleWheelEvent(message);
			break;
		default:
			break;
	}

	if (message->WasDropped() && fDropTargetComponent != NULL) {
		_HandleDnDDrop(message);
		return;
	}

	BView::MessageReceived(message);
}


void
ContentView::MouseDown(BPoint point)
{
	SetMouseEventMask(B_POINTER_EVENTS, 0);
	_HandleMouseEvent(Window()->CurrentMessage(), point);
	BView::MouseDown(point);
}


void
ContentView::MouseMoved(BPoint point, uint32 transit, const BMessage* message)
{
	// If the mouse entered the view we should reset our previous buttons
	if (transit == B_ENTERED_VIEW)
		get_mouse(NULL, &fPreviousButtons);

	_HandleMouseEvent(Window()->CurrentMessage(), point, transit, message);
	BView::MouseMoved(point, transit, message);
}


void
ContentView::MouseUp(BPoint point)
{
	_HandleMouseEvent(Window()->CurrentMessage(), point);
	BView::MouseUp(point);
}


void
ContentView::_HandleKeyEvent(BMessage* message)
{
	int64 when = 0;
	message->FindInt64("when", &when);
	int32 modifiers = 0;
	message->FindInt32("modifiers", &modifiers);
	int32 key = 0;
	message->FindInt32("key", &key);	

	jint id;
	jint mods = 0;
	jint keyCode = java_awt_event_KeyEvent_VK_UNDEFINED;
	jint keyLocation = java_awt_event_KeyEvent_KEY_LOCATION_UNKNOWN;
	jchar keyChar = java_awt_event_KeyEvent_CHAR_UNDEFINED;

	if (message->what == B_KEY_DOWN || message->what == B_UNMAPPED_KEY_DOWN) {
		id = java_awt_event_KeyEvent_KEY_PRESSED;
	} else {
		id = java_awt_event_KeyEvent_KEY_RELEASED;
	}

	mods = ConvertInputModifiersToJava(modifiers);
	ConvertKeyCodeToJava(key, &keyCode, &keyLocation);

	JNIEnv* env = GetEnv();

	jstring keyString;
	BString bytes;
	if (message->FindString("bytes", &bytes) == B_OK) {
		keyString = env->NewStringUTF(bytes.String());
	} else {
		keyString = NULL;
	}

	env->CallVoidMethod(fPlatformWindow, eventKeyMethod, id, (jlong)(when / 1000),
		mods, keyCode, keyString, keyLocation);
	EXCEPTION_CHECK(env);

	if (keyString != NULL) {
		env->DeleteLocalRef(keyString);
	}
}


void
ContentView::_HandleMouseEvent(BMessage* message, BPoint point, uint32 transit,
	const BMessage* dragMessage)
{
	// Get out early if this message is useless
	int32 buttons = 0;
	message->FindInt32("buttons", &buttons);
	int32 buttonChange = buttons ^ fPreviousButtons;
	if (point == fPreviousPoint && buttonChange == 0)
		return;

	fPreviousPoint = point;
	fPreviousButtons = buttons;

	point.x += fLeftInset;
	point.y += fTopInset;

	BPoint screenPoint = ConvertToScreen(point);
	int64 when = 0;
	message->FindInt64("when", &when);
	int32 clicks = 0;
	message->FindInt32("clicks", &clicks);

	int32 modifiers = 0;
	if (message->FindInt32("modifiers", &modifiers) != B_OK)
		modifiers = ::modifiers();
	jint mods = ConvertInputModifiersToJava(modifiers)
		| ConvertMouseMaskToJava(buttons);

	jint id = 0;
	switch (message->what) {
		case B_MOUSE_DOWN:
			id = java_awt_event_MouseEvent_MOUSE_PRESSED;
			break;
		case B_MOUSE_UP:
			id = java_awt_event_MouseEvent_MOUSE_RELEASED;
			break;
		case B_MOUSE_MOVED:
			if (transit == B_ENTERED_VIEW)
				id = java_awt_event_MouseEvent_MOUSE_ENTERED;
			else if (transit == B_EXITED_VIEW)
				id = java_awt_event_MouseEvent_MOUSE_EXITED;
			else {
				if (buttons != 0)
					id = java_awt_event_MouseEvent_MOUSE_DRAGGED;
				else
					id = java_awt_event_MouseEvent_MOUSE_MOVED;
			}
			break;
		default:
			break;
	}

	_HandleDropTargetMessage(id, transit, dragMessage, (jint)point.x,
		(jint)point.y);
	_HandleDragSourceMessage(id, transit, dragMessage, (jint)point.x,
		(jint)point.y, mods);

	jint button = java_awt_event_MouseEvent_NOBUTTON;
	if (id == java_awt_event_MouseEvent_MOUSE_PRESSED
			|| id == java_awt_event_MouseEvent_MOUSE_RELEASED)
		button = ConvertMouseButtonToJava(buttonChange);
	else if (id == java_awt_event_MouseEvent_MOUSE_DRAGGED)
		button = ConvertMouseButtonToJava(buttons);

	JNIEnv* env = GetEnv();
	env->CallVoidMethod(fPlatformWindow, eventMouseMethod, id,
		(jlong)(when / 1000), mods, (jint)point.x, (jint)point.y,
		(jint)screenPoint.x, (jint)screenPoint.y, (jint)clicks, button);
	EXCEPTION_CHECK(env);
}


void
ContentView::_HandleWheelEvent(BMessage* message)
{
	int64 when = 0;
	message->FindInt64("when", &when);
	int32 modifiers = ::modifiers();
	
	uint32 buttons = 0;
	BPoint point;
	GetMouse(&point, &buttons);

	point.x += fLeftInset;
	point.y += fTopInset;

	jint mods = ConvertInputModifiersToJava(modifiers);
	if (buttons & B_PRIMARY_MOUSE_BUTTON)
		mods |= java_awt_event_MouseEvent_BUTTON1_DOWN_MASK;
	if (buttons & B_SECONDARY_MOUSE_BUTTON)
		mods |= java_awt_event_MouseEvent_BUTTON2_DOWN_MASK;
	if (buttons & B_TERTIARY_MOUSE_BUTTON)
		mods |= java_awt_event_MouseEvent_BUTTON3_DOWN_MASK;

	float wheelRotation = 0;
	message->FindFloat("be:wheel_delta_y", &wheelRotation);
	
	jint scrollType = java_awt_event_MouseWheelEvent_WHEEL_UNIT_SCROLL;
	if ((modifiers & (B_OPTION_KEY | B_COMMAND_KEY | B_CONTROL_KEY)) != 0)
	    scrollType = java_awt_event_MouseWheelEvent_WHEEL_BLOCK_SCROLL;

	jint scrollAmount = 3;

	JNIEnv* env = GetEnv();
	env->CallVoidMethod(fPlatformWindow, eventWheelMethod, (jlong)(when / 1000),
		mods, (jint)point.x, (jint)point.y, scrollType, scrollAmount,
		(jint)wheelRotation, (jdouble)wheelRotation);
	EXCEPTION_CHECK(env);
}


static jobjectArray getFormatArray(JNIEnv* env, BMessage* dragMessage) {
	jclassLocal stringClazz(env, env->FindClass("java/lang/String"));
	if (stringClazz == NULL)
		return NULL;

	int32 count = dragMessage->CountNames(B_MIME_TYPE);

	jobjectArray result = env->NewObjectArray(count, stringClazz, NULL);
	if (result == NULL)
		return NULL;

	char* nameFound;
	for (int32 i = 0; dragMessage->GetInfo(B_MIME_TYPE, i, &nameFound, NULL,
			NULL) == B_OK; i++) {
		jstringLocal name(env, env->NewStringUTF(nameFound));
		if (name == NULL) {
			env->DeleteLocalRef(result);
			return NULL;
		}

		env->SetObjectArrayElement(result, i, name);
	}

	return result;
}


DECLARE_JAVA_CLASS(dropTargetClazz, "sun/hawt/HaikuDropTargetContextPeer")


void
ContentView::_HandleDropTargetMessage(jint id, uint32 transit,
	const BMessage* dragMessage, jint x, jint y)
{
	if (fDropTargetComponent == NULL)
		return;

	// The drop target context should have been cleared on drop or exit.
	if (dragMessage == NULL) {
		assert(fDropTargetContext == NULL);
		return;
	}

	// No messages if we're outside the view.
	if (transit == B_OUTSIDE_VIEW)
		return;

	// Shouldn't have a drag message in these cases.
	assert(id != java_awt_event_MouseEvent_MOUSE_MOVED
		&& id != java_awt_event_MouseEvent_MOUSE_PRESSED
		&& id != java_awt_event_MouseEvent_MOUSE_RELEASED);

	if (dragMessage->what != B_SIMPLE_DATA && dragMessage->what != B_MIME_DATA)
		return;

	// If we're entering the view, we shouldn't have an existing context.
	assert(transit != B_ENTERED_VIEW || fDropTargetContext == NULL);

	JNIEnv* env = GetEnv();

	if (fDropTargetContext == NULL) {
		// Copy the message and set up the formats array.
		BMessage* copyMessage = new(std::nothrow) BMessage(*dragMessage);
		assert(copyMessage != NULL);

		jobjectArrayLocal formats(env, getFormatArray(env, copyMessage));
		assert(formats != NULL);

		DECLARE_STATIC_VOID_JAVA_METHOD(getContext, dropTargetClazz,
			"getDropTargetContextPeer",
			"(Ljava/awt/Component;J[Ljava/lang/String;)Lsun/hawt/HaikuDropTargetContextPeer;");

		jobjectLocal context(env, env->CallStaticObjectMethod(clazz, getContext,
			fDropTargetComponent, ptr_to_jlong(copyMessage), (jobjectArray)formats));

		EXCEPTION_CHECK(env);

		fDropTargetContext = env->NewGlobalRef(context);
		assert(fDropTargetContext != NULL);
	}

	if (transit == B_INSIDE_VIEW) {
		DoCallback(fDropTargetContext, "handleMotion", "(II)V", x, y);
	} else if (transit == B_ENTERED_VIEW) {
		DoCallback(fDropTargetContext, "handleEnter", "(II)V", x, y);
	} else if (transit == B_EXITED_VIEW) {
		DoCallback(fDropTargetContext, "handleExit", "(II)V", x, y);

		// Clear out the context for the next drag. The context retains
		// responsibility for deleting the message and other resources.
		env->DeleteGlobalRef(fDropTargetContext);
		fDropTargetContext = NULL;
	}

	EXCEPTION_CHECK(env);
}


DECLARE_JAVA_CLASS(dragSourceClazz, "sun/hawt/HaikuDragSourceContextPeer")


void
ContentView::_HandleDragSourceMessage(jint id, uint32 transit,
	const BMessage* dragMessage, jint x, jint y, jint mods)
{
	if (fDragSourceContext == NULL)
		return;

	// If we have a drag source context, this should either be a drag
	// or release event.
	assert(id == java_awt_event_MouseEvent_MOUSE_DRAGGED
		|| id == java_awt_event_MouseEvent_MOUSE_RELEASED);

	JNIEnv* env = GetEnv();

	if (id == java_awt_event_MouseEvent_MOUSE_RELEASED) {
		DoCallback(fDragSourceContext, "dragDropFinished", "(ZIII)V",
			(jboolean)true, java_awt_dnd_DnDConstants_ACTION_COPY, x, y);
		env->DeleteGlobalRef(fDragSourceContext);
		fDragSourceContext = NULL;
	} else {
		switch (transit) {
		case B_ENTERED_VIEW:
			DoCallback(fDragSourceContext, "dragEnter", "(IIII)V",
				java_awt_dnd_DnDConstants_ACTION_COPY, modifiers, x, y);
			break;
		case B_EXITED_VIEW:
			DoCallback(fDragSourceContext, "dragExit", "(II)V",	x, y);
			break;
		case B_INSIDE_VIEW:
		case B_OUTSIDE_VIEW:
			DoCallback(fDragSourceContext, "dragMouseMoved", "(IIII)V",
				java_awt_dnd_DnDConstants_ACTION_COPY, modifiers, x, y);
			break;
		default:
			assert(false);
		}
	}

	EXCEPTION_CHECK(env);
}


void
ContentView::_HandleDnDDrop(BMessage* message)
{
	// We should already have a context.
	assert(fDropTargetContext != NULL);

	JNIEnv* env = GetEnv();

	BPoint dropPoint = message->DropPoint();
	ConvertFromScreen(&dropPoint);

	dropPoint.y += fTopInset;
	dropPoint.x += fLeftInset;

	DoCallback(fDropTargetContext, "handleDrop", "(II)V", (jint)dropPoint.x,
		(jint)dropPoint.y);
	EXCEPTION_CHECK(env);

	env->DeleteGlobalRef(fDropTargetContext);
	fDropTargetContext = NULL;
}
