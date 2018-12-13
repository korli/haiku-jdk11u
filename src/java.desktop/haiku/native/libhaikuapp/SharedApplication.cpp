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

#include "SharedApplication.h"

#include <assert.h>
#include <pthread.h>

#include <Application.h>


static pthread_once_t app_started = PTHREAD_ONCE_INIT;
static status_t app_result = B_ERROR;

static const uint32 kAttachToJVM = '_AVM';


class SharedApplication : public BApplication {
public:
    SharedApplication(const char* signature);
    virtual ~SharedApplication() { }

    virtual void ReadyToRun();
    void WaitUntilReady();

    virtual void MessageReceived(BMessage* message);
    JNIEnv* GetJNIEnv();
    JNIEnv* AttachToJVM(JavaVM* vm);
    void DetachFromJVM();

private:
    pthread_mutex_t fWaitMutex;
    pthread_cond_t fWaitCond;

    pthread_mutex_t fAttachMutex;
    pthread_cond_t fAttachCond;

    bool fReady;
    JNIEnv* fEnv;
};


SharedApplication::SharedApplication(const char* signature)
    :
    BApplication(signature),
    fReady(false),
    fEnv(NULL)
{
    pthread_mutex_init(&fWaitMutex, NULL);
    pthread_cond_init(&fWaitCond, NULL);
    pthread_mutex_init(&fAttachMutex, NULL);
    pthread_cond_init(&fAttachCond, NULL);
}


void
SharedApplication::ReadyToRun()
{
    pthread_mutex_lock(&fWaitMutex);
    fReady = true;
    pthread_cond_broadcast(&fWaitCond);
    pthread_mutex_unlock(&fWaitMutex);
}


void
SharedApplication::WaitUntilReady()
{
    pthread_mutex_lock(&fWaitMutex);
    while (!fReady) {
        pthread_cond_wait(&fWaitCond, &fWaitMutex);
    }
    pthread_mutex_unlock(&fWaitMutex);
}


void
SharedApplication::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kAttachToJVM: {
            JavaVM* vm = NULL;
            status_t status = message->FindPointer("javavm", (void**)&vm);
            assert(status == B_OK);

            pthread_mutex_lock(&fAttachMutex);
            jint result = vm->AttachCurrentThreadAsDaemon((void**)&fEnv, NULL);
            assert(result == 0);

            pthread_cond_broadcast(&fAttachCond);
            pthread_mutex_unlock(&fAttachMutex);
            break;
        }
    }

    BApplication::MessageReceived(message);
}


JNIEnv*
SharedApplication::AttachToJVM(JavaVM* vm)
{
    pthread_mutex_lock(&fAttachMutex);

    if (fEnv != NULL) {
        pthread_mutex_unlock(&fAttachMutex);
        return fEnv;
    }

    BMessage message(kAttachToJVM);
    message.AddPointer("javavm", vm);

    PostMessage(&message, NULL);

    while (fEnv == NULL) {
        pthread_cond_wait(&fAttachCond, &fAttachMutex);
    }

    pthread_mutex_unlock(&fAttachMutex);
    return fEnv;
}


void
SharedApplication::DetachFromJVM()
{
    if (fEnv) {
        JavaVM* vm;
        fEnv->GetJavaVM(&vm);
        vm->DetachCurrentThread();
        fEnv = NULL;
    }
}


JNIEnv*
SharedApplication::GetJNIEnv()
{
    return fEnv;
}


void*
ApplicationThread(void* arg)
{
    SharedApplication* app = (SharedApplication*)arg;
    app->LockLooper();
    app->Run();
    app->DetachFromJVM();
    delete app;

    return NULL;
}


static void StartApplication() {
    pthread_attr_t attr;
    app_result = pthread_attr_init(&attr);
    if (app_result != 0) {
        return;
    }

    app_result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (app_result != 0) {
        return;
    }

    SharedApplication* app = new SharedApplication("application/java-awt-app");
    if (app->InitCheck() != B_OK) {
        delete app;
        app_result = app->InitCheck();
        return;
    }

    app->UnlockLooper();

    pthread_t thread;
    app_result = pthread_create(&thread, &attr, ApplicationThread, (void*)app);
    if (app_result != 0) {
        delete app;
        return;
    }

    app->WaitUntilReady();
    app_result = B_OK;
}


extern "C" {

JNIEXPORT JNIEnv* AttachToJVM(JavaVM* vm)
{
    SharedApplication* app = (SharedApplication*)be_app;
    return app->AttachToJVM(vm);
}


JNIEXPORT JNIEnv* GetJNIEnv()
{
    SharedApplication* app = (SharedApplication*)be_app;
    return app->GetJNIEnv();
}


JNIEXPORT status_t RunApplication()
{
    pthread_once(&app_started, StartApplication);
    return app_result;
}

}
