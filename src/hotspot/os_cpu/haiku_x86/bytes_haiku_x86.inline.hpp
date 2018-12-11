/*
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
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
 *
 */

#ifndef OS_CPU_HAIKU_X86_VM_BYTES_HAIKU_X86_INLINE_HPP
#define OS_CPU_HAIKU_X86_VM_BYTES_HAIKU_X86_INLINE_HPP

// Efficient swapping of data bytes from Java byte
// ordering to native byte ordering and vice versa.
inline u2 Bytes::swap_u2(u2 x) {
  __asm__ ("rorw $8, %w0" : "+r" (x));
  return x;
}

inline u4 Bytes::swap_u4(u4 x) {
  __asm__ ("bswap %0" : "+r" (x));
  return x;
}

#ifdef AMD64
inline u8 Bytes::swap_u8(u8 x) {
  __asm__ ("bswapq %0" : "+r" (x));
  return x;
}
#else
inline u8 Bytes::swap_u8(u8 x) {
  u4* dwords = (u4*)&x;
  __asm__ ("xchg %0, %1;"
           "bswap %0;"
           "bswap %1;"
           : "+r" (dwords[0]),
             "+r" (dwords[1]));
  return x;
}
#endif // !AMD64

#endif // OS_CPU_HAIKU_X86_VM_BYTES_HAIKU_X86_INLINE_HPP
