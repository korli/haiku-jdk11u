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

extern "C" {
#include "splashscreen_impl.h"
}

#include <assert.h>
#include <fcntl.h>
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sizecalc.h>

#include <Bitmap.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <View.h>
#include <Window.h>

#include <SharedApplication.h>


#define WINDOW(s) ((SplashWindow*)s->window)


static const uint32 kAdvanceFrame = 'ADVA';
static const uint32 kUpdateImage = 'UPDA';
static const uint32 kReconfigure = 'RECO';


class SplashWindow : public BWindow {
public:
    SplashWindow(Splash* splash, BRect frame, const char* title);
    virtual ~SplashWindow();

    virtual void MessageReceived(BMessage* message);
    void UpdateImage();
    void ScheduleNextFrame();

private:
    Splash* fSplash;
    BView* fImageView;
    BMessageRunner* fFrameRunner;
};


SplashWindow::SplashWindow(Splash* splash, BRect frame, const char* title)
    :
    BWindow(frame, title, B_NO_BORDER_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, 0),
    fSplash(splash),
    fFrameRunner(NULL)
{
    fImageView = new BView(frame, title, B_FOLLOW_ALL, 0);
    AddChild(fImageView);
}


SplashWindow::~SplashWindow()
{
    delete fFrameRunner;
}


void
SplashWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kAdvanceFrame:
            ScheduleNextFrame();
            break;
        case kUpdateImage:
            UpdateImage();
            break;
        case kReconfigure:
            SplashLock(fSplash);
            ResizeTo(fSplash->width - 1, fSplash->height - 1);
            CenterOnScreen();
            SplashUnlock(fSplash);
            break;
    }

    return BWindow::MessageReceived(message);
}

void
SplashWindow::UpdateImage()
{
    SplashLock(fSplash);
    SplashUpdateScreenData(fSplash);

    BRect bounds = BRect(0, 0, fSplash->width - 1, fSplash->height - 1);
    BBitmap* bitmap = new BBitmap(bounds, 0, B_RGBA32, fSplash->screenStride,
        B_MAIN_SCREEN_ID);
    memcpy(bitmap->Bits(), fSplash->screenData, fSplash->screenStride * fSplash->height);

    SplashUnlock(fSplash);

    bool locked = LockLooper();
    assert(locked);

    fImageView->SetViewBitmap(bitmap);
    UnlockLooper();

    delete bitmap;
}


void
SplashWindow::ScheduleNextFrame()
{
    SplashLock(fSplash);
    if (fSplash->isVisible > 0 && fSplash->currentFrame >= 0 &&
            SplashTime() >= fSplash->time + fSplash->frames[fSplash->currentFrame].delay) {
        SplashNextFrame(fSplash);
        UpdateImage();
    }

    if (fSplash->isVisible > 0 && SplashIsStillLooping(fSplash)) {
        BMessenger messenger(NULL, this);
        assert(messenger.InitCheck() == B_OK);

        unsigned long long timeout = fSplash->time
            + fSplash->frames[fSplash->currentFrame].delay - SplashTime();

        delete fFrameRunner;
        fFrameRunner = new BMessageRunner(messenger, new BMessage(kAdvanceFrame),
            timeout, 1);
    }
    SplashUnlock(fSplash);
}


extern "C" {


unsigned
SplashTime(void) {
    struct timeval tv;
    struct timezone tz;
    unsigned long long msec;

    gettimeofday(&tv, &tz);
    msec = (unsigned long long) tv.tv_sec * 1000 +
        (unsigned long long) tv.tv_usec / 1000;

    return (unsigned) msec;
}


/* Could use npt but decided to cut down on linked code size */
char* SplashConvertStringAlloc(const char* in, int* size) {
    const char     *codeset;
    const char     *codeset_out;
    iconv_t         cd;
    size_t          rc;
    char           *buf = NULL, *out;
    size_t          bufSize, inSize, outSize;
    const char* old_locale;

    if (!in) {
        return NULL;
    }
    old_locale = setlocale(LC_ALL, "");

    codeset = nl_langinfo(CODESET);
    if ( codeset == NULL || codeset[0] == 0 ) {
        goto done;
    }
    /* we don't need BOM in output so we choose native BE or LE encoding here */
    codeset_out = (platformByteOrder()==BYTE_ORDER_MSBFIRST) ?
        "UCS-2BE" : "UCS-2LE";

    cd = iconv_open(codeset_out, codeset);
    if (cd == (iconv_t)-1 ) {
        goto done;
    }
    inSize = strlen(in);
    buf = (char*)SAFE_SIZE_ARRAY_ALLOC(malloc, inSize, 2);
    if (!buf) {
        return NULL;
    }
    bufSize = inSize*2; // need 2 bytes per char for UCS-2, this is
                        // 2 bytes per source byte max
    out = buf; outSize = bufSize;
    /* linux iconv wants char** source and solaris wants const char**...
       cast to void* */
    rc = iconv(cd, (char**)&in, &inSize, &out, &outSize);
    iconv_close(cd);

    if (rc == (size_t)-1) {
        free(buf);
        buf = NULL;
    } else {
        if (size) {
            *size = (bufSize-outSize)/2; /* bytes to wchars */
        }
    }
done:
    setlocale(LC_ALL, old_locale);
    return buf;
}


void
SplashInitPlatform(Splash* splash) {
    pthread_mutex_init(&splash->lock, NULL);

    splash->byteAlignment = 1;
    initFormat(&splash->screenFormat, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    splash->screenFormat.byteOrder = BYTE_ORDER_LSBFIRST;
    splash->screenFormat.depthBytes = 4;

    status_t result = RunApplication();
    assert(result == B_OK);

    BRect frame(0, 0, 0, 0);
    splash->window = (SplashWindow*)new SplashWindow(splash, frame, "Splashscreen");
}


void
SplashCleanupPlatform(Splash* splash) {
    splash->maskRequired = 0;
}


void
SplashDonePlatform(Splash* splash) {
    // Window already deleted by Quit()
    pthread_mutex_destroy(&splash->lock);
}


void
SplashLock(Splash* splash) {
    pthread_mutex_lock(&splash->lock);
}


void
SplashUnlock(Splash* splash) {
    pthread_mutex_unlock(&splash->lock);
}


void
SplashInitFrameShape(Splash* splash, int imageIndex) {
    // Not supported
}


void
SplashCreateThread(Splash* splash) {
    WINDOW(splash)->Show();
    WINDOW(splash)->PostMessage(kAdvanceFrame);
    WINDOW(splash)->PostMessage(kReconfigure);
}


void
SplashClosePlatform(Splash* splash) {
    bool locked = WINDOW(splash)->Lock();
    assert(locked);

    WINDOW(splash)->Quit();
    SplashDone(splash);
}


void
SplashUpdate(Splash* splash) {
    WINDOW(splash)->PostMessage(kUpdateImage);
}


void
SplashReconfigure(Splash* splash) {
    WINDOW(splash)->PostMessage(kReconfigure);
}

}
