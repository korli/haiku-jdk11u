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

package sun.hawt;

import java.awt.*;
import java.awt.event.*;
import java.awt.image.*;
import java.awt.peer.*;
import java.util.*;

import sun.awt.*;
import sun.lwawt.*;
import sun.lwawt.LWWindowPeer.PeerType;

import sun.java2d.*;

public class HaikuPlatformWindow implements PlatformWindow {

    private Window target;
    private LWWindowPeer peer;
    private HaikuPlatformWindow owner;
    private long nativeWindow;
    private HaikuWindowSurfaceData surfaceData;
    private PeerType peerType;

    private HaikuPlatformWindow blocker;
    private Set<HaikuPlatformWindow> blockedWindows = new HashSet<HaikuPlatformWindow>();
    private Set<HaikuPlatformWindow> currentBlocked = new HashSet<HaikuPlatformWindow>();

    private int windowLook;
    private int windowFeel;
    private int windowFlags;

    // Window looks
    private static final int B_BORDERED_WINDOW_LOOK = 20;
    private static final int B_NO_BORDER_WINDOW_LOOK = 19;
    private static final int B_TITLED_WINDOW_LOOK = 1;
    private static final int B_DOCUMENT_WINDOW_LOOK = 11;
    private static final int B_MODAL_WINDOW_LOOK = 3;
    private static final int B_FLOATING_WINDOW_LOOK = 7;

    // Window feels
    private static final int B_NORMAL_WINDOW_FEEL = 0;
    private static final int B_MODAL_APP_WINDOW_FEEL = 1;
    private static final int B_MODAL_ALL_WINDOW_FEEL = 3;
    private static final int B_FLOATING_APP_WINDOW_FEEL = 4;
    private static final int B_FLOATING_ALL_WINDOW_FEEL = 6;

    // Window flags
    private static final int B_NOT_MOVABLE = 0x0001;
    private static final int B_NOT_RESIZABLE = 0x0002;
    private static final int B_NOT_CLOSABLE = 0x0020;
    private static final int B_NOT_ZOOMABLE = 0x0040;
    private static final int B_NOT_MINIMIZABLE = 0x4000;
    private static final int B_AVOID_FRONT = 0x0080;
    private static final int B_AVOID_FOCUS = 0x2000;

    private final Point location = new Point();
    private final Insets insets = new Insets(0, 0, 0, 0);

    // Sometimes while hidden the window gets a spurious FrameResized event
    // with incorrect bounds. We cache the correct bounds here and set them
    // when the window is shown again.
    private final Rectangle bounds = new Rectangle();

    private static native void initIDs();

    private native long nativeInit(int look, int feel, int flags);
    private native long nativeGetDrawable(long nativeWindow);
    private native void nativeSetBounds(long nativeWindow, int x, int y,
        int width, int height);
    private native void nativeSetVisible(long nativeWindow, boolean visible);
    private native void nativeDispose(long nativeWindow);
    private native void nativeFocus(long nativeWindow);
    private native void nativeSetWindowState(long nativeWindow,
        int windowState);
    private native void nativeSetTitle(long nativeWindow, String title);
    private native void nativeToFront(long nativeWindow);
    private native void nativeToBack(long nativeWindow);
    private native void nativeSetMenuBar(long nativeWindow, long menuBarPtr);
    private native void nativeSetSizeConstraints(long nativeWindow,
        int minWidth, int minHeight, int maxWidth, int maxHeight);
    private native void nativeGetInsets(long nativeWindow, Insets insets);
    private native boolean nativeIsActive(long nativeWindow);
    private native void nativeBlock(long nativeWindow, long nativeBlockee);
    private native void nativeUnblock(long nativeWindow, long nativeBlockee);

    private native void nativeSetWindowFeel(long nativeWindow, int feel);
    private native void nativeSetWindowFlags(long nativeWindow, int flags);

    private native void nativeSetDropTarget(long nativeWindow, Component target);
    private native long nativeGetView(long nativeWindow);

    static {
        initIDs();
    }

    public HaikuPlatformWindow(final PeerType peerType) {
        this.peerType = peerType;
    }

    /*
     * Delegate initialization (create native window and all the
     * related resources).
     */
    @Override
    public void initialize(Window target, LWWindowPeer peer, PlatformWindow owner) {
        this.peer = peer;
        this.target = target;
        this.owner = (HaikuPlatformWindow)owner;

        setInitialFlags();
        nativeWindow = nativeInit(windowLook, windowFeel, windowFlags);
        nativeGetInsets(nativeWindow, insets);
    }

    private void setInitialFlags() {
        windowFeel = B_NORMAL_WINDOW_FEEL;
        windowLook = B_DOCUMENT_WINDOW_LOOK;
        windowFlags = 0;

        final boolean isFrame = (target instanceof Frame);
        final boolean isDialog = (target instanceof Dialog);
        final boolean isPopup = (target.getType() == Window.Type.POPUP);
        if (isDialog) {
            windowLook = B_TITLED_WINDOW_LOOK;
            windowFlags |= B_NOT_MINIMIZABLE;
        }

        // Either java.awt.Frame or java.awt.Dialog can be undecorated, however java.awt.Window always is undecorated.
        final boolean isUndecorated = isFrame ? ((Frame)target).isUndecorated() : (isDialog ? ((Dialog)target).isUndecorated() : true);
        if (isUndecorated) {
            windowLook = B_NO_BORDER_WINDOW_LOOK;
        }

        // Either java.awt.Frame or java.awt.Dialog can be resizable, however java.awt.Window is never resizable
        final boolean isResizable = isFrame ? ((Frame)target).isResizable() : (isDialog ? ((Dialog)target).isResizable() : false);
        if (!isResizable) {
            windowFlags |= B_NOT_RESIZABLE;
            windowFlags |= B_NOT_ZOOMABLE;
        }

        if (!isNativelyFocusableWindow()) {
            windowFlags |= B_AVOID_FOCUS;
        }

        if (target.isAlwaysOnTop()) {
            windowFeel = B_FLOATING_ALL_WINDOW_FEEL;
        }

        if (Window.Type.UTILITY.equals(target.getType())) {
            windowLook = B_FLOATING_WINDOW_LOOK;
        }
    }

    @Override
    public long getLayerPtr() {
        return nativeWindow;
    }

    @Override
    public LWWindowPeer getPeer() {
        return peer;
    }

    @Override
    public void dispose() {
        nativeDispose(nativeWindow);
    }

    @Override
    public void setVisible(boolean visible) {
        nativeSetVisible(nativeWindow, visible);
        if (visible) {
            setBounds(bounds.x, bounds.y, bounds.width, bounds.height);
        }
    }

    @Override
    public void setTitle(String title) {
        nativeSetTitle(nativeWindow, title);
    }

    @Override
    public void setBounds(int x, int y, int width, int height) {
        bounds.setBounds(x, y, width, height);
        nativeSetBounds(nativeWindow, x, y, width, height);
    }

    @Override
    public Point getLocationOnScreen() {
        return (Point)location.clone();
    }

    @Override
    public Insets getInsets() {
        return (Insets)insets.clone();
    }

    @Override
    public void toFront() {
        nativeToFront(nativeWindow);
    }
    
    @Override
    public void toBack() {
        nativeToBack(nativeWindow);
    }

    @Override
    public void setMenuBar(MenuBar menuBar) {
        HaikuMenuBar peer = (HaikuMenuBar)LWToolkit.targetToPeer(menuBar);
        if (peer != null) {
            nativeSetMenuBar(nativeWindow, peer.getModel());
        } else {
            nativeSetMenuBar(nativeWindow, 0);
        }
    }

    @Override
    public void setAlwaysOnTop(boolean value) {
        if (value) {
            nativeSetWindowFeel(nativeWindow, B_FLOATING_ALL_WINDOW_FEEL);
        } else {
            nativeSetWindowFeel(nativeWindow, B_NORMAL_WINDOW_FEEL);
        }
    }

    /*
     * Our focus model is synthetic and only non-simple window
     * may become natively focusable window.
     */
    private boolean isNativelyFocusableWindow() {
        if (peer == null) {
            return false;
        }

        return !peer.isSimpleWindow() && target.getFocusableWindowState();
    }

    @Override
    public void updateFocusableWindowState() {
        final boolean isFocusable = isNativelyFocusableWindow();
         if (isFocusable) {
             windowFlags &= ~B_AVOID_FOCUS;
         } else {
             windowFlags |= B_AVOID_FOCUS;
         }
         nativeSetWindowFlags(nativeWindow, windowFlags);
    }

    @Override
    public void setResizable(boolean resizable) {
        if (resizable) {
            windowFlags |= B_NOT_RESIZABLE;
        } else {
            windowFlags &= ~B_NOT_RESIZABLE;
        }
        nativeSetWindowFlags(nativeWindow, windowFlags);
    }

    @Override
    public void setSizeConstraints(int minWidth, int minHeight, int maxWidth,
            int maxHeight) {
        nativeSetSizeConstraints(nativeWindow, minWidth, minHeight, maxWidth,
            maxHeight);
    }

    @Override
    public void setWindowState(int windowState) {
        nativeSetWindowState(nativeWindow, windowState);
    }

    @Override
    public boolean rejectFocusRequest(FocusEvent.Cause cause) {
        return false;
    }

    @Override
    public boolean requestWindowFocus() {
        nativeFocus(nativeWindow);
        return true;
    }

    @Override
    public GraphicsDevice getGraphicsDevice() {
        return getGraphicsConfiguration().getDevice();
    }

    @Override
    public boolean isActive() {
        return nativeIsActive(nativeWindow);
    }

    @Override
    public Graphics transformGraphics(Graphics g) {
        g.translate(-insets.left, -insets.top);
        return g;
    }

    @Override
    public SurfaceData replaceSurfaceData() {
        if (surfaceData == null) {
            long drawable = nativeGetDrawable(nativeWindow);
            surfaceData = new HaikuWindowSurfaceData(
                HaikuWindowSurfaceData.typeDefault, getColorModel(),
                getGraphicsConfiguration(), drawable, this);
        }
        return surfaceData;
    }

    public ColorModel getColorModel() {
        return getGraphicsConfiguration().getColorModel();
    }

    public GraphicsConfiguration getGraphicsConfiguration() {
        return HaikuGraphicsConfig.getDefaultConfiguration();
    }

    public Rectangle getBounds() {
        return peer.getBounds();
    }

    public Window getTarget() {
        return target;
    }

    public void addDropTarget(Component target) {
        nativeSetDropTarget(nativeWindow, target);
    }

    public void removeDropTarget() {
        nativeSetDropTarget(nativeWindow, null);
    }

    private long getView() {
        return nativeGetView(nativeWindow);
    }

    // ================
    // Blocking support
    // ================

    @Override
    public void setModalBlocked(boolean blocked) {
        if (blocked) {
            assert blocker == null : "Setting a new blocker on an already blocked window";

            // unblock our windows, but keep them in the blockee list as we
            // may need to reblock them later when our blocker goes away
            for (HaikuPlatformWindow window : blockedWindows) {
                doUnblock(window);
            }

            blocker = (HaikuPlatformWindow)peer.getFirstBlocker().
                getPlatformWindow();
            assert blocker != null : "Being asked to block but have no blocker";
            blocker.block(this);

            for (HaikuPlatformWindow window : blockedWindows) {
                blocker.block(window);
            }
        } else {
            assert blocker != null : "Being asked to unblock but have no blocker";
            blocker.unblock(this);
            blocker = null;

            for (HaikuPlatformWindow window : blockedWindows) {
                doBlock(window);
            }
        }
    }

    private void block(HaikuPlatformWindow window) {
        if (!blockedWindows.contains(window)) {
            blockedWindows.add(window);

            // we can't have a hierarchy of modal windows, so we need to
            // propogate the block up to the foremost blocker
            if (blocker != null) {
                blocker.block(window);
            } else {
                doBlock(window);
            }
        }
    }

    private void unblock(HaikuPlatformWindow window) {
        if (blockedWindows.contains(window)) {
            blockedWindows.remove(window);

            if (blocker != null) {
                blocker.unblock(window);
            } else {
                doUnblock(window);
            }
        }
    }

    private void doBlock(HaikuPlatformWindow window) {
        if (!currentBlocked.contains(window)) {
            currentBlocked.add(window);
            nativeBlock(nativeWindow, window.getLayerPtr());
        }
    }

    private void doUnblock(HaikuPlatformWindow window) {
        if (currentBlocked.contains(window)) {
            nativeUnblock(nativeWindow, window.getLayerPtr());
            currentBlocked.remove(window);
        }
    }

    // =======================================
    // Unimplemented/unsupported functionality
    // =======================================

    @Override
    public void updateIconImages() {
        // not supported
    }

    @Override
    public void setOpacity(float opacity) {
        // not supported
    }

    @Override
    public void setOpaque(boolean isOpaque) {
        // not supported
    }

    @Override
    public void enterFullScreenMode() {
        // not supported
    }

    @Override
    public void exitFullScreenMode() {
        // not supported
    }

    @Override
    public boolean isFullScreenMode() {
        return false;
    }

    @Override
    public SurfaceData getScreenSurface() {
        return null;
    }

    @Override
    public FontMetrics getFontMetrics(Font f) {
        (new RuntimeException("unimplemented")).printStackTrace();
        return null;
    }

    @Override
    public boolean isUnderMouse() {
        return false;
    }

    // =====================
    // Native code callbacks
    // =====================

    public void eventRepaint(int x, int y, int width, int height) {
        peer.notifyExpose(new Rectangle(x, y, width, height));
    }

    public void eventReshape(int x, int y, int width, int height) {
        if (nativeWindow == 0)
            return;
        location.x = x;
        location.y = y;

        peer.notifyReshape(x, y, width, height);
    }

    public void eventMaximize(boolean maximize) {
        peer.notifyZoom(maximize);
    }

    public void eventMinimize(boolean minimize) {
        peer.notifyIconify(minimize);
    }

    public void eventWindowClosing() {
        if (peer.getBlocker() == null)  {
            peer.postEvent(new WindowEvent(target, WindowEvent.WINDOW_CLOSING));
        }
    }

    public void eventActivate(boolean activated) {
        peer.notifyActivation(activated, null);
    }

    public void eventKey(int id, long when, int modifiers, int keyCode,
            String keyString, int keyLocation) {
        char keyChar = KeyEvent.CHAR_UNDEFINED;
        if (keyString != null && keyString.length() > 0) {
            keyChar = keyString.charAt(0);
        }

        peer.notifyKeyEvent(id, when, modifiers, keyCode, keyChar,
            keyLocation);

        if (id == KeyEvent.KEY_PRESSED && keyChar != KeyEvent.CHAR_UNDEFINED) {
            peer.notifyKeyEvent(KeyEvent.KEY_TYPED, when, modifiers,
                KeyEvent.VK_UNDEFINED, keyChar, KeyEvent.KEY_LOCATION_UNKNOWN);
        }
    }

    public void eventMouse(int id, long when, int modifiers, int x, int y,
            int screenX, int screenY, int clicks, int button) {
        // Popup = press button 2 or ctrl press button 1
        boolean popup = id == MouseEvent.MOUSE_PRESSED
            && (button == MouseEvent.BUTTON3 ||
                   (button == MouseEvent.BUTTON1 &&
               (modifiers & MouseEvent.CTRL_DOWN_MASK) != 0));

        peer.notifyMouseEvent(id, when, button, x, y, screenX, screenY,
            modifiers, clicks, popup, null);
    }

    public void eventWheel(long when, int modifiers, int x, int y, int scrollType,
            int scrollAmount, int wheelRotation, double preciseWheelRotation) {
        peer.notifyMouseWheelEvent(when, x, y, modifiers, scrollType,
            scrollAmount, wheelRotation, preciseWheelRotation, null);
    }

    public void updateInsets(int left, int top, int right, int bottom) {
        insets.set(top, left, bottom, right);
        peer.updateInsets(insets);
    }

}
