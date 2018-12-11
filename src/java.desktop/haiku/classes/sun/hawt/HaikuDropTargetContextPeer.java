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
import java.awt.dnd.DropTarget;
import java.awt.dnd.DnDConstants;

import sun.awt.datatransfer.DataTransferer;
import sun.awt.dnd.SunDropTargetContextPeer;
import sun.awt.dnd.SunDropTargetEvent;

import javax.swing.*;


final class HaikuDropTargetContextPeer extends SunDropTargetContextPeer {

    private Component target;
    private long nativeMessage;
    private long[] formats;

    private DropTarget insideTarget = null;

    private native void nativeFreeMessage(long message);
    private native byte[] nativeGetData(long message, String format);

    private HaikuDropTargetContextPeer(Component target, long nativeMessage,
            String[] nativeFormats) {
        super();

        this.target = target;
        this.nativeMessage = nativeMessage;
        this.formats = new long[nativeFormats.length];

        HaikuDataTransferer transferer = (HaikuDataTransferer)DataTransferer.getInstance();
        for (int i = 0; i < nativeFormats.length; i++) { 
            this.formats[i] = transferer.getFormatForNativeAsLong(nativeFormats[i]);
        }
    }

    protected static HaikuDropTargetContextPeer getDropTargetContextPeer(
            Component target, long nativeMessage, String[] formats) {
        return new HaikuDropTargetContextPeer(target, nativeMessage, formats);
    }

    @Override
    protected Object getNativeData(long format) {
    	HaikuDataTransferer transferer = (HaikuDataTransferer)DataTransferer.getInstance();
    	String nativeFormat = transferer.getNativeForFormat(format);
        return nativeGetData(nativeMessage, nativeFormat);
    }

    @Override
    protected void doDropDone(boolean success, int dropAction, boolean isLocal) {
        long nativeMessage = getNativeDragContext();
        if (nativeMessage != 0) {
            nativeFreeMessage(nativeMessage);
        }
    }

    protected void handleEnter(int x, int y) {
        handleEnterMessage(target, x, y, DnDConstants.ACTION_COPY,
            DnDConstants.ACTION_COPY, formats, nativeMessage);
    }

    protected void handleMotion(int x, int y) {
        handleMotionMessage(target, x, y, DnDConstants.ACTION_COPY,
            DnDConstants.ACTION_COPY, formats, nativeMessage);
    }

    protected void handleExit(int x, int y) {
        handleExitMessage(target, nativeMessage);

        // Although the exit event is dispatched asynchronously, no data can
        // be read from an exit event, so we can free the message now.
        nativeFreeMessage(nativeMessage);
    }

    protected void handleDrop(int x, int y) {
        handleDropMessage(target, x, y, DnDConstants.ACTION_COPY,
            DnDConstants.ACTION_COPY, formats, nativeMessage);
    }

    // We need to take care of dragEnter and dragExit messages because
    // native system generates them only for heavyweights
    @Override
    protected void processMotionMessage(SunDropTargetEvent event, boolean operationChanged) {
        boolean eventInsideTarget = isEventInsideTarget(event);
        if (event.getComponent().getDropTarget() == insideTarget) {
            if (!eventInsideTarget) {
                processExitMessage(event);
                return;
            }
        } else {
            if (eventInsideTarget) {
                processEnterMessage(event);
            } else {
                return;
            }
        }
        super.processMotionMessage(event, operationChanged);
    }

    /**
     * Could be called when DnD enters a heavyweight or synthesized in processMotionMessage
     */
    @Override
    protected void processEnterMessage(SunDropTargetEvent event) {
        Component c = event.getComponent();
        DropTarget dt = event.getComponent().getDropTarget();
        if (isEventInsideTarget(event)
                && dt != insideTarget
                && c.isShowing()
                && dt != null
                && dt.isActive()) {
            insideTarget = dt;
            super.processEnterMessage(event);
        }
    }

    /**
     * Could be called when DnD exits a heavyweight or synthesized in processMotionMessage
     */
    @Override
    protected void processExitMessage(SunDropTargetEvent event) {
        if (event.getComponent().getDropTarget() == insideTarget) {
            insideTarget = null;
            super.processExitMessage(event);
        }
    }

    @Override
    protected void processDropMessage(SunDropTargetEvent event) {
        if (isEventInsideTarget(event)) {
            super.processDropMessage(event);
            insideTarget = null;
        }
    }

    private boolean isEventInsideTarget(SunDropTargetEvent event) {
        Component eventSource = event.getComponent();
        Point screenPoint = event.getPoint();
        SwingUtilities.convertPointToScreen(screenPoint, eventSource);
        Point locationOnScreen = eventSource.getLocationOnScreen();
        Rectangle screenBounds = new Rectangle(locationOnScreen.x,
                                               locationOnScreen.y,
                                               eventSource.getWidth(),
                                               eventSource.getHeight());
        return screenBounds.contains(screenPoint);
    }

    @Override
    protected int postDropTargetEvent(Component component, int x, int y,
            int dropAction, int actions, long[] formats, long nativeCtxt,
            int eventID, boolean dispatchType) {
        // On Haiku all the DnD events should be asynchronous
        return super.postDropTargetEvent(component, x, y, dropAction, actions,
            formats, nativeCtxt, eventID, !SunDropTargetContextPeer.DISPATCH_SYNC);
    }
}
