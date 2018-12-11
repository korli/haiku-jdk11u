/*
 * Copyright (c) 2012, 2013, Oracle and/or its affiliates. All rights reserved.
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

package sun.nio.ch;

import java.io.IOException;
import jdk.internal.misc.Unsafe;

/**
 * Provides access to the Haiku wait_for_objects facility.
 */

class HQueue {
    private HQueue() { }

    private static final Unsafe unsafe = Unsafe.getUnsafe();

    /**
     *  struct object_wait_info {
     *        int32           object;         // ID of the object
     *        uint16          type;           // type of the object
     *        uint16          events;         // events mask
     *  };
     */
    private static final int SIZEOF_HQUEUEEVENT    = heventSize();
    private static final int OFFSET_OBJECT         = objectOffset();
    private static final int OFFSET_TYPE           = typeOffset();
    private static final int OFFSET_EVENTS         = eventsOffset();

    // types
    static final int TYPE_FD     = 0;
    static final int TYPE_SEMAPHORE = 1;
    static final int TYPE_PORT = 2;
    static final int TYPE_THREAD = 3;

    // events
    static final int EVENT_READ  = 0x0001;
    static final int EVENT_WRITE = 0x0002;
    static final int EVENT_ERROR = 0x0004;
    static final int EVENT_PRIORITY_READ = 0x0008;
    static final int EVENT_HIGH_PRIORITY_READ = 0x0010;
    static final int EVENT_HIGH_PRIORITY_WRITE = 0x0020;
    static final int EVENT_DISCONNECTED = 0x0040;
    static final int EVENT_ACQUIRE_SEMAPHORE = 0x0001;
    static final int EVENT_INVALID = 0x1000;

    /**
     * Allocates a poll array to handle up to {@code count} events.
     */
    static long allocatePollArray(int count) {
        return unsafe.allocateMemory(count * SIZEOF_HQUEUEEVENT);
    }

    /**
     * Free a poll array
     */
    static void freePollArray(long address) {
        unsafe.freeMemory(address);
    }

    /**
     * Returns hevent[i].
     */
    static long getEvent(long address, int i) {
        return address + (SIZEOF_HQUEUEEVENT*i);
    }

    /**
     * Returns the file descriptor from a hevent (assuming to be in object field)
     */
    static int getDescriptor(long address) {
        return unsafe.getInt(address + OFFSET_OBJECT);
    }

    static int getType(long address) {
        return unsafe.getShort(address + OFFSET_TYPE);
    }

    static int getEvents(long address) {
        return unsafe.getShort(address + OFFSET_EVENTS);
    }

    /**
     * Sets the file descriptor from a hevent (assuming to be in object field)
     */
    static void setDescriptor(long address, int object) {
        unsafe.putInt(address + OFFSET_OBJECT, object);
    }

    static void setType(long address, int type) {
        unsafe.putShort(address + OFFSET_TYPE, (short)type);
    }

    static void setEvents(long address, int events) {
        unsafe.putShort(address + OFFSET_EVENTS, (short)events);
    }

    // -- Native methods --

    private static native int heventSize();

    private static native int objectOffset();

    private static native int typeOffset();

    private static native int eventsOffset();

    static native int heventPoll(long pollAddress, int nevents)
        throws IOException;

    static {
        IOUtil.load();
    }
}
