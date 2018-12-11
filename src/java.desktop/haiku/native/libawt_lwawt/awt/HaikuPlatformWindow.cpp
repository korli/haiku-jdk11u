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

#include <jlong.h>
#include <jni.h>

#include "sun_hawt_HaikuPlatformWindow.h"
#include "java_awt_Frame.h"

#include "HaikuPlatformWindow.h"

#include <kernel/OS.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <View.h>
#include <Window.h>

#include "Utilities.h"


// The amount of extra size we give the drawable so we're not reallocating it
// all the time
static const int kResizeBuffer = 200;

static jfieldID pointXField;
static jfieldID pointYField;
static jfieldID rectXField;
static jfieldID rectYField;
static jfieldID rectWidthField;
static jfieldID rectHeightField;
static jfieldID insetsLeftField;
static jfieldID insetsTopField;
static jfieldID insetsRightField;
static jfieldID insetsBottomField;

static jmethodID eventActivateMethod;
static jmethodID eventMaximizeMethod;
static jmethodID eventMinimizeMethod;
static jmethodID eventReshapeMethod;
static jmethodID eventWindowClosingMethod;
static jmethodID updateInsetsMethod;

// Used by ContentView
jmethodID eventKeyMethod;
jmethodID eventMouseMethod;
jmethodID eventWheelMethod;


extern "C" {


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_initIDs(JNIEnv *env, jclass clazz)
{
	jclass pointClazz = env->FindClass("java/awt/Point");
	pointXField = env->GetFieldID(pointClazz, "x", "I");
	pointYField = env->GetFieldID(pointClazz, "y", "I");

	jclass rectClazz = env->FindClass("java/awt/Rectangle");
	rectXField = env->GetFieldID(rectClazz, "x", "I");
	rectYField = env->GetFieldID(rectClazz, "y", "I");
	rectWidthField = env->GetFieldID(rectClazz, "width", "I");
	rectHeightField = env->GetFieldID(rectClazz, "height", "I");

	jclass insetsClazz = env->FindClass("java/awt/Insets");
	insetsLeftField = env->GetFieldID(insetsClazz, "left", "I");
	insetsTopField = env->GetFieldID(insetsClazz, "top", "I");
	insetsRightField = env->GetFieldID(insetsClazz, "right", "I");
	insetsBottomField = env->GetFieldID(insetsClazz, "bottom", "I");

	eventActivateMethod = env->GetMethodID(clazz, "eventActivate", "(Z)V");
	eventMaximizeMethod = env->GetMethodID(clazz, "eventMaximize", "(Z)V");
	eventMinimizeMethod = env->GetMethodID(clazz, "eventMinimize", "(Z)V");
	eventReshapeMethod = env->GetMethodID(clazz, "eventReshape", "(IIII)V");
	eventWindowClosingMethod = env->GetMethodID(clazz, "eventWindowClosing", "()V");
	updateInsetsMethod = env->GetMethodID(clazz, "updateInsets", "(IIII)V");

	eventKeyMethod = env->GetMethodID(clazz, "eventKey", "(IJIILjava/lang/String;I)V");
	eventMouseMethod = env->GetMethodID(clazz, "eventMouse", "(IJIIIIIII)V");
	eventWheelMethod = env->GetMethodID(clazz, "eventWheel", "(JIIIIIID)V");
}


JNIEXPORT jlong JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeInit(JNIEnv *env, jobject thiz,
	jint look, jint feel, jint flags)
{
	jobject javaWindow = env->NewWeakGlobalRef(thiz);

	PlatformWindow* window = new PlatformWindow(javaWindow,	(window_look)look,
		(window_feel)feel, (uint32)flags);

	window->Hide();
	window->Show();
	window->blocked_windows = 0;

	return ptr_to_jlong(window);
}


JNIEXPORT jlong JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeGetDrawable(JNIEnv *env, jobject thiz,
	jlong nativeWindow)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	if (!window->LockLooper())
		return 0;

	Drawable* drawable = window->GetView()->GetDrawable();
	window->UnlockLooper();
	return ptr_to_jlong(drawable);
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeSetBounds(JNIEnv *env, jobject thiz,
	jlong nativeWindow, jint x, jint y, jint width, jint height)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	BRect frameRect = BRect(x, y, x + width - 1, y + height - 1);
	if (!window->LockLooper())
		return;

	// given coordinates include the decorator frame, transform to
	// the client area
	BRect rect = window->TransformFromFrame(frameRect);
	window->MoveTo(rect.left, rect.top);
	window->ResizeTo(rect.IntegerWidth(), rect.IntegerHeight());
	window->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeSetVisible(JNIEnv *env, jobject thiz,
	jlong nativeWindow, jboolean visible)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);

	if (!window->LockLooper())
		return;

	if (visible) {
		while (window->IsHidden())
			window->Show();
	} else {
		while (!window->IsHidden())
			window->Hide();
	}
	window->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeDispose(JNIEnv *env, jobject thiz,
	jlong nativeWindow)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);

	if (!window->LockLooper())
		// Uh-oh, leak the window?
		return;

	window->Dispose(env);
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeFocus(JNIEnv *env, jobject thiz,
	jlong nativeWindow)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);

	if (!window->LockLooper())
		return;
	window->Focus();
	window->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeSetWindowState(JNIEnv *env,
	jobject thiz, jlong nativeWindow, jint windowState)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);

	if (!window->LockLooper())
		return;
	window->SetState(windowState);
	window->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeSetTitle(JNIEnv *env, jobject thiz,
	jlong nativeWindow, jstring title)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);

	const char* name = env->GetStringUTFChars(title, NULL);
	if (name == NULL)
		return;

	if (!window->LockLooper())
		return;
	window->SetTitle(name);
	window->UnlockLooper();
	env->ReleaseStringUTFChars(title, name);
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeToFront(JNIEnv *env, jobject thiz,
	jlong nativeWindow)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);

	if (!window->LockLooper())
		return;
	window->Activate();
	window->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeToBack(JNIEnv *env, jobject thiz,
	jlong nativeWindow)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);

	if (!window->LockLooper())
		return;
	window->SendBehind(NULL);
	window->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeSetMenuBar(JNIEnv *env, jobject thiz,
	jlong nativeWindow, jlong menuBarItemPtr)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	BMenuItem* menuBarItem = (BMenuItem*)jlong_to_ptr(menuBarItemPtr);
	BMenuBar* menuBar = (BMenuBar*)menuBarItem->Submenu();

	if (!window->LockLooper())
		return;
	window->SetMenuBar(menuBar);
	window->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeSetSizeConstraints(JNIEnv *env,
	jobject thiz, jlong nativeWindow, jint minWidth, jint minHeight,
	jint maxWidth, jint maxHeight)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	if (!window->LockLooper())
		return;

	window->AdjustDimensions(minWidth, minHeight);
	window->AdjustDimensions(maxWidth, maxHeight);
	if (maxWidth > 32768) {
		maxWidth = 32768;
	}
	if (maxHeight > 32768) {
		maxHeight = 32768;
	}

	window->SetSizeLimits(minWidth, maxWidth, minHeight, maxHeight);
	window->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeGetInsets(JNIEnv *env, jobject thiz,
	jlong nativeWindow, jobject javaInsets)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);

	if (!window->LockLooper())
		return;

	Insets insets = window->GetInsets();
	window->UnlockLooper();

	int topInsets = insets.top + insets.menu;
	env->SetIntField(javaInsets, insetsLeftField, (jint)insets.left);
	env->SetIntField(javaInsets, insetsTopField, (jint)topInsets);
	env->SetIntField(javaInsets, insetsRightField, (jint)insets.right);
	env->SetIntField(javaInsets, insetsBottomField, (jint)insets.bottom);
}


JNIEXPORT jboolean JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeIsActive(JNIEnv *env, jobject thiz,
	jlong nativeWindow)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	return window->IsActive();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeBlock(JNIEnv *env, jobject thiz,
	jlong nativeWindow, jlong nativeBlockee)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	if (!window->LockLooper())
		return;

	PlatformWindow* blockee = (PlatformWindow*)jlong_to_ptr(nativeBlockee);
	if (!blockee->LockLooper()) {
		window->UnlockLooper();
		return;
	}

	if (window->blocked_windows++ == 0) {
		window->SetFeel(B_MODAL_SUBSET_WINDOW_FEEL);
	}
	window->AddToSubset(blockee);

	window->UnlockLooper();
	blockee->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeUnblock(JNIEnv *env, jobject thiz,
	jlong nativeWindow, jlong nativeBlockee)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	if (!window->LockLooper())
		return;

	PlatformWindow* blockee = (PlatformWindow*)jlong_to_ptr(nativeBlockee);
	if (!blockee->LockLooper()) {
		window->UnlockLooper();
		return;
	}

	if (--window->blocked_windows == 0) {
		window->SetFeel(B_NORMAL_WINDOW_FEEL);
	}
	window->RemoveFromSubset(blockee);

	window->UnlockLooper();
	blockee->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeSetDropTarget(JNIEnv *env,
	jobject thiz, jlong nativeWindow, jobject target)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	if (!window->LockLooper())
		return;

	jobject targetRef = target != NULL ? env->NewWeakGlobalRef(target) : NULL;
	window->SetDropTarget(targetRef);
	window->UnlockLooper();
}


JNIEXPORT jlong JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeGetView(JNIEnv *env,
	jobject thiz, jlong nativeWindow)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	if (!window->LockLooper())
		return ptr_to_jlong(NULL);

	ContentView* view = window->GetView();
	window->UnlockLooper();
	return ptr_to_jlong(view);
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeSetWindowFlags(JNIEnv *env,
	jobject thiz, jlong nativeWindow, jint flags)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	if (!window->LockLooper())
		return;

	status_t result = window->SetFlags((uint32)flags);
	assert(result == B_OK);
	window->UnlockLooper();
}


JNIEXPORT void JNICALL
Java_sun_hawt_HaikuPlatformWindow_nativeSetWindowFeel(JNIEnv *env,
	jobject thiz, jlong nativeWindow, jint feel)
{
	PlatformWindow* window = (PlatformWindow*)jlong_to_ptr(nativeWindow);
	if (!window->LockLooper())
		return;

	status_t result = window->SetFeel((window_feel)feel);
	assert(result == B_OK);
	window->UnlockLooper();
}


}


PlatformWindow::PlatformWindow(jobject platformWindow, window_look look,
	window_feel feel, uint32 flags)
	:
	BWindow(BRect(0, 0, 0, 0), NULL, look, feel, flags),
	fView(new ContentView(platformWindow)),
	fPlatformWindow(platformWindow),
	fMenuBar(NULL),
	fInsets(GetInsets())
{
	AddChild(fView);

	// After this initial bounds set the view will size itself
	// to match the frame
	BRect bounds = Bounds();
	fView->MoveTo(0, 0);
	fView->ResizeTo(bounds.IntegerWidth(), bounds.IntegerHeight());
}


ContentView*
PlatformWindow::GetView()
{
	return fView;
}


void
PlatformWindow::SetState(int state)
{
	// Should a maximize cancel out a minimize?
	// Or should it be 'behind-the-scenes' maximized,
	// so it shows as maximized when it becomes unminimized?
	
	if ((state & java_awt_Frame_ICONIFIED) != 0)
		Minimize(true);

	if ((state & java_awt_Frame_MAXIMIZED_BOTH) != 0) {
		if (!fMaximized)
			BWindow::Zoom();
	}

	// Normal should cancel out the two other states
	if ((state & java_awt_Frame_NORMAL) != 0) {
		Minimize(false);
		if (fMaximized)
			BWindow::Zoom();
	}
}


void
PlatformWindow::Dispose(JNIEnv* env)
{
	env->DeleteWeakGlobalRef(fPlatformWindow);
	Quit();
}


void
PlatformWindow::SetMenuBar(BMenuBar* menuBar)
{
	if (menuBar != NULL) {
		AddChild(menuBar);
		BRect bounds = menuBar->Bounds();
		fView->MoveTo(0, bounds.bottom + 1);
	} else {
		fView->MoveTo(0, 0);
	}

	if (fMenuBar != NULL)
		RemoveChild(fMenuBar);
	fMenuBar = menuBar;

	// The insets probably changed
	_UpdateInsets();
}


Insets
PlatformWindow::GetInsets()
{
	float borderWidth = 5.0;
	float tabHeight = 21.0;

	BMessage settings;
	if (GetDecoratorSettings(&settings) == B_OK) {
		BRect tabRect;
		if (settings.FindRect("tab frame", &tabRect) == B_OK)
			tabHeight = tabRect.Height();
		settings.FindFloat("border width", &borderWidth);
	} else {
		// probably no-border window look
		if (Look() == B_NO_BORDER_WINDOW_LOOK) {
			borderWidth = 0.0;
			tabHeight = 0.0;
		}
		// else use fall-back values from above
	}

	int menuHeight = 0;
	if (fMenuBar != NULL) {
		BRect bounds = fMenuBar->Bounds();
		menuHeight = bounds.IntegerHeight() + 1;
	}

	fView->SetInsets(borderWidth, tabHeight + borderWidth + menuHeight);

	// +1's here?
	return Insets(borderWidth, tabHeight + borderWidth,
		borderWidth, borderWidth, menuHeight);
}


void
PlatformWindow::Focus()
{
	Activate();
}


void
PlatformWindow::StartDrag(BMessage* message, jobject dragSource)
{
	fView->StartDrag(message, dragSource);
}


void
PlatformWindow::SetDropTarget(jobject target)
{
	fView->SetDropTarget(target);
}


void
PlatformWindow::WindowActivated(bool activated)
{
	fView->MakeFocus(activated);

	JNIEnv* env = GetEnv();
	env->CallVoidMethod(fPlatformWindow, eventActivateMethod, activated);
}


void
PlatformWindow::FrameMoved(BPoint origin)
{
	_Reshape(false);
	BWindow::FrameMoved(origin);
}


void
PlatformWindow::FrameResized(float width, float height)
{
	_Reshape(true);
	BWindow::FrameResized(width, height);
}


void
PlatformWindow::Minimize(bool minimize)
{
	JNIEnv* env = GetEnv();
	env->CallVoidMethod(fPlatformWindow, eventMinimizeMethod, minimize);

	BWindow::Minimize(minimize);
}


bool
PlatformWindow::QuitRequested()
{
	JNIEnv* env = GetEnv();
	env->CallVoidMethod(fPlatformWindow, eventWindowClosingMethod);
	
	// According to WindowEvent docs, we should ignore the user's request to
	// quit and send an event to the peer. AWT will then decide what to do.
	return false;
}


void
PlatformWindow::Zoom(BPoint origin, float width, float height)
{
	// For whatever reason, there is no getter for this so we record the state
	// ourselves.
	fMaximized = !fMaximized;

	JNIEnv* env = GetEnv();
	env->CallVoidMethod(fPlatformWindow, eventMaximizeMethod, fMaximized);
	
	BWindow::Zoom(origin, width, height);
}


BRect
PlatformWindow::TransformToFrame(BRect rect)
{
	return BRect(rect.left - fInsets.left, rect.top - fInsets.top,
		rect.right + fInsets.right, rect.bottom + fInsets.bottom);
}


BRect
PlatformWindow::TransformFromFrame(BRect rect)
{
	return BRect(rect.left + fInsets.left, rect.top + fInsets.top,
		rect.right - fInsets.right, rect.bottom - fInsets.bottom);
}


void
PlatformWindow::AdjustDimensions(int& width, int& height)
{
	width -= fInsets.left + fInsets.right;
	height -= fInsets.top + fInsets.bottom;
	if (width < 0) {
		width = 0;
	}
	if (height < 0) {
		height = 0;
	}
}

void
PlatformWindow::_Reshape(bool resize)
{
	BRect bounds = Frame();

	int w = bounds.IntegerWidth() + 2;
	int h = bounds.IntegerHeight() + 2;

	if (resize) {
		Drawable* drawable = fView->GetDrawable();
		if (drawable->Lock()) {
			if (!drawable->IsValid()
					|| w > drawable->Width()
					|| h > drawable->Height()
					|| w + kResizeBuffer * 2 < drawable->Width()
					|| h + kResizeBuffer * 2 < drawable->Height()) {
				drawable->Allocate(w + kResizeBuffer, h + kResizeBuffer);
			}
			drawable->Unlock();
		}
	}

	// transform bounds to include the decorations
	BRect frame = TransformToFrame(bounds);

	// Should probably execute handler on EDT instead of unlocking here
	UnlockLooper();

	JNIEnv* env = GetEnv();
	env->CallVoidMethod(fPlatformWindow, eventReshapeMethod, (int)frame.left,
		(int)frame.top, frame.IntegerWidth() + 1, frame.IntegerHeight() + 1);

	LockLooper();
}


void
PlatformWindow::_UpdateInsets()
{
	fInsets = GetInsets();
	int topInset = fInsets.top + fInsets.menu;

	fView->SetInsets(fInsets.left, fInsets.top + fInsets.menu);

	JNIEnv* env = GetEnv();
	env->CallVoidMethod(fPlatformWindow, updateInsetsMethod, fInsets.left,
		topInset, fInsets.right, fInsets.bottom);
}
