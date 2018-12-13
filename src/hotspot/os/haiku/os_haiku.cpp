/*
 * Copyright (c) 1999, 2015, Oracle and/or its affiliates. All rights reserved.
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

// no precompiled headers
#include "jvm.h"
#include "classfile/classLoader.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "code/icBuffer.hpp"
#include "code/vtableStubs.hpp"
#include "compiler/compileBroker.hpp"
#include "compiler/disassembler.hpp"
#include "interpreter/interpreter.hpp"
#include "logging/log.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/filemap.hpp"
#include "oops/oop.inline.hpp"
#include "os_haiku.inline.hpp"
#include "os_share_haiku.hpp"
#include "prims/jniFastGetField.hpp"
#include "prims/jvm_misc.hpp"
#include "runtime/arguments.hpp"
#include "runtime/atomic.hpp"
#include "runtime/extendedPC.hpp"
#include "runtime/globals.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/objectMonitor.hpp"
#include "runtime/orderAccess.hpp"
#include "runtime/osThread.hpp"
#include "runtime/perfMemory.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/statSampler.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/thread.inline.hpp"
#include "runtime/threadCritical.hpp"
#include "runtime/timer.hpp"
#include "semaphore_posix.hpp"
#include "services/attachListener.hpp"
#include "services/memTracker.hpp"
#include "services/runtimeService.hpp"
#include "utilities/decoder.hpp"
#include "utilities/defaultStream.hpp"
#include "utilities/elf.h"
#include "utilities/events.hpp"
#include "utilities/elfFile.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/vmError.hpp"

// put OS-includes here
# include <sys/types.h>
# include <sys/mman.h>
# include <sys/stat.h>
# include <sys/select.h>
# include <pthread.h>
# include <signal.h>
# include <errno.h>
# include <dlfcn.h>
# include <stdio.h>
# include <unistd.h>
# include <sys/resource.h>
# include <pthread.h>
# include <sys/stat.h>
# include <sys/time.h>
# include <sys/times.h>
# include <sys/utsname.h>
# include <sys/socket.h>
# include <sys/wait.h>
# include <pwd.h>
# include <poll.h>
# include <semaphore.h>
# include <fcntl.h>
# include <string.h>
# include <sys/ipc.h>
# include <stdint.h>
# include <inttypes.h>
# include <sys/ioctl.h>

#include <kernel/image.h>
#include <kernel/OS.h>


#define MAX_SECS 100000000
#define MAX_PATH B_FILE_NAME_LENGTH

// for timer info max values which include all bits
#define ALL_64_BITS CONST64(0xFFFFFFFFFFFFFFFF)
#define SEC_IN_MICROSECS 1000000LL

////////////////////////////////////////////////////////////////////////////////
// global variables
julong os::Haiku::_physical_memory = 0;

pthread_t os::Haiku::_main_thread;
int os::Haiku::_page_size = 0;

static jlong initial_time_count = 0;
static int clock_tics_per_sec = 0;

// For diagnostics to print a message once. see run_periodic_checks
static sigset_t check_signal_done;
static bool check_signals = true;

/* Signal number used to suspend/resume a thread */

/* do not use any signal number less than SIGSEGV, see 4355769 */
static int SR_signum = SIGUSR2;
sigset_t SR_sigset;

// Declarations
static void unpackTime(timespec* absTime, bool isAbsolute, jlong time);

// utility functions

static int SR_initialize();

julong os::available_memory() {
  return Haiku::available_memory();
}

julong os::Haiku::available_memory() {
  system_info si;
  get_system_info(&si);

  return (julong)(si.max_pages - si.used_pages) * page_size();
}

julong os::physical_memory() {
  return Haiku::physical_memory();
}

// Return true if user is running as root.
bool os::have_special_privileges() {
  return true;
}

// Cpu architecture string
#if   defined(ZERO)
static char cpu_arch[] = ZERO_LIBARCH;
#elif defined(IA64)
static char cpu_arch[] = "ia64";
#elif defined(IA32)
static char cpu_arch[] = "i386";
#elif defined(AMD64)
static char cpu_arch[] = "amd64";
#elif defined(ARM)
static char cpu_arch[] = "arm";
#elif defined(PPC32)
static char cpu_arch[] = "ppc";
#elif defined(PPC64)
static char cpu_arch[] = "ppc64";
#elif defined(SPARC)
#  ifdef _LP64
static char cpu_arch[] = "sparcv9";
#  else
static char cpu_arch[] = "sparc";
#  endif
#else
#error Add appropriate cpu_arch setting
#endif

void os::Haiku::initialize_system_info() {
  system_info si;
  get_system_info(&si);
  set_processor_count(si.cpu_count);
  _physical_memory = (julong)si.max_pages * (julong)_page_size;
  assert(processor_count() > 0, "linux error");
}

void os::init_system_properties_values() {
  // The next steps are taken in the product version:
  //
  // Obtain the JAVA_HOME value from the location of libjvm.so.
  // This library should be located at:
  // <JAVA_HOME>/jre/lib/<arch>/{client|server}/libjvm.so.
  //
  // If "/jre/lib/" appears at the right place in the path, then we
  // assume libjvm.so is installed in a JDK and we use this path.
  //
  // Otherwise exit with message: "Could not create the Java virtual machine."
  //
  // The following extra steps are taken in the debugging version:
  //
  // If "/jre/lib/" does NOT appear at the right place in the path
  // instead of exit check for $JAVA_HOME environment variable.
  //
  // If it is defined and we are able to locate $JAVA_HOME/jre/lib/<arch>,
  // then we append a fake suffix "hotspot/libjvm.so" to this path so
  // it looks like libjvm.so is installed there
  // <JAVA_HOME>/jre/lib/<arch>/hotspot/libjvm.so.
  //
  // Otherwise exit.
  //
  // Important note: if the location of libjvm.so changes this
  // code needs to be changed accordingly.

// See ld(1):
//      The linker uses the following search paths to locate required
//      shared libraries:
//        1: ...
//        ...
//        7: The default directories, normally /lib and /usr/lib.
#define DEFAULT_LIBPATH "%%A/lib:/boot/home/config/non-packaged/lib:/boot/home/config/lib:/boot/system/non-packaged/lib:/boot/system/lib"

// Base path of extensions installed on the system.
#define SYS_EXT_DIR     "/usr/java/packages"
#define EXTENSIONS_DIR  "/lib/ext"

  // Buffer that fits several sprintfs.
  // Note that the space for the colon and the trailing null are provided
  // by the nulls included by the sizeof operator.
  const size_t bufsize =
    MAX2((size_t)MAXPATHLEN,  // For dll_dir & friends.
         (size_t)MAXPATHLEN + sizeof(EXTENSIONS_DIR) + sizeof(SYS_EXT_DIR) + sizeof(EXTENSIONS_DIR)); // extensions dir
  char *buf = (char *)NEW_C_HEAP_ARRAY(char, bufsize, mtInternal);

  // sysclasspath, java_home, dll_dir
  {
    char *pslash;
    os::jvm_path(buf, bufsize);

    // Found the full path to libjvm.so.
    // Now cut the path to <java_home>/jre if we can.
    pslash = strrchr(buf, '/');
    if (pslash != NULL) {
      *pslash = '\0';            // Get rid of /libjvm.so.
    }
    pslash = strrchr(buf, '/');
    if (pslash != NULL) {
      *pslash = '\0';            // Get rid of /{client|server|hotspot}.
    }
    Arguments::set_dll_dir(buf);

    if (pslash != NULL) {
      pslash = strrchr(buf, '/');
      if (pslash != NULL) {
        *pslash = '\0';        // Get rid of /lib.
      }
    }
    Arguments::set_java_home(buf);
    set_boot_path('/', ':');
  }

  // Where to look for native libraries.
  //
  // Note: Due to a legacy implementation, most of the library path
  // is set in the launcher. This was to accomodate linking restrictions
  // on legacy Linux implementations (which are no longer supported).
  // Eventually, all the library path setting will be done here.
  //
  // However, to prevent the proliferation of improperly built native
  // libraries, the new path component /usr/java/packages is added here.
  // Eventually, all the library path setting will be done here.
  {
    // Get the user setting of LD_LIBRARY_PATH, and prepended it. It
    // should always exist (until the legacy problem cited above is
    // addressed).
    const char *v = ::getenv("LIBRARY_PATH");
    const char *v_colon = ":";
    if (v == NULL) { v = ""; v_colon = ""; }
    // That's +1 for the colon and +1 for the trailing '\0'.
    char *ld_library_path = (char *)NEW_C_HEAP_ARRAY(char,
                                                     strlen(v) + 1 +
                                                     sizeof(SYS_EXT_DIR) + sizeof("/lib/") + sizeof(DEFAULT_LIBPATH) + 1,
                                                     mtInternal);
    sprintf(ld_library_path, "%s%s" SYS_EXT_DIR "/lib:" DEFAULT_LIBPATH, v, v_colon);
    Arguments::set_library_path(ld_library_path);
    FREE_C_HEAP_ARRAY(char, ld_library_path);
  }

  // Extensions directories.
  sprintf(buf, "%s" EXTENSIONS_DIR ":" SYS_EXT_DIR EXTENSIONS_DIR, Arguments::get_java_home());
  Arguments::set_ext_dirs(buf);

  FREE_C_HEAP_ARRAY(char, buf);

#undef DEFAULT_LIBPATH
#undef SYS_EXT_DIR
#undef EXTENSIONS_DIR
}

////////////////////////////////////////////////////////////////////////////////
// breakpoint support

void os::breakpoint() {
  BREAKPOINT;
}

extern "C" void breakpoint() {
  // use debugger to set breakpoint here
}

////////////////////////////////////////////////////////////////////////////////
// signal support

debug_only(static bool signal_sets_initialized = false);
static sigset_t unblocked_sigs, vm_sigs;

bool os::Haiku::is_sig_ignored(int sig) {
      struct sigaction oact;
      sigaction(sig, (struct sigaction*)NULL, &oact);
      void* ohlr = oact.sa_sigaction ? CAST_FROM_FN_PTR(void*,  oact.sa_sigaction)
                                     : CAST_FROM_FN_PTR(void*,  oact.sa_handler);
      if (ohlr == CAST_FROM_FN_PTR(void*, SIG_IGN))
           return true;
      else
           return false;
}

void os::Haiku::signal_sets_init() {
  // Should also have an assertion stating we are still single-threaded.
  assert(!signal_sets_initialized, "Already initialized");
  // Fill in signals that are necessarily unblocked for all threads in
  // the VM. Currently, we unblock the following signals:
  // SHUTDOWN{1,2,3}_SIGNAL: for shutdown hooks support (unless over-ridden
  //                         by -Xrs (=ReduceSignalUsage));
  // BREAK_SIGNAL which is unblocked only by the VM thread and blocked by all
  // other threads. The "ReduceSignalUsage" boolean tells us not to alter
  // the dispositions or masks wrt these signals.
  // Programs embedding the VM that want to use the above signals for their
  // own purposes must, at this time, use the "-Xrs" option to prevent
  // interference with shutdown hooks and BREAK_SIGNAL thread dumping.
  // (See bug 4345157, and other related bugs).
  // In reality, though, unblocking these signals is really a nop, since
  // these signals are not blocked by default.
  sigemptyset(&unblocked_sigs);
  sigaddset(&unblocked_sigs, SIGILL);
  sigaddset(&unblocked_sigs, SIGSEGV);
  sigaddset(&unblocked_sigs, SIGBUS);
  sigaddset(&unblocked_sigs, SIGFPE);
#if defined(PPC64)
  sigaddset(&unblocked_sigs, SIGTRAP);
#endif
  sigaddset(&unblocked_sigs, SR_signum);

  if (!ReduceSignalUsage) {
   if (!os::Haiku::is_sig_ignored(SHUTDOWN1_SIGNAL)) {
      sigaddset(&unblocked_sigs, SHUTDOWN1_SIGNAL);
   }
   if (!os::Haiku::is_sig_ignored(SHUTDOWN2_SIGNAL)) {
      sigaddset(&unblocked_sigs, SHUTDOWN2_SIGNAL);
   }
   if (!os::Haiku::is_sig_ignored(SHUTDOWN3_SIGNAL)) {
      sigaddset(&unblocked_sigs, SHUTDOWN3_SIGNAL);
   }
  }
  // Fill in signals that are blocked by all but the VM thread.
  sigemptyset(&vm_sigs);
  if (!ReduceSignalUsage)
    sigaddset(&vm_sigs, BREAK_SIGNAL);
  debug_only(signal_sets_initialized = true);

}

// These are signals that are unblocked while a thread is running Java.
// (For some reason, they get blocked by default.)
sigset_t* os::Haiku::unblocked_signals() {
  assert(signal_sets_initialized, "Not initialized");
  return &unblocked_sigs;
}

// These are the signals that are blocked while a (non-VM) thread is
// running Java. Only the VM thread handles these signals.
sigset_t* os::Haiku::vm_signals() {
  assert(signal_sets_initialized, "Not initialized");
  return &vm_sigs;
}

void os::Haiku::hotspot_sigmask(Thread* thread) {

  //Save caller's signal mask before setting VM signal mask
  sigset_t caller_sigmask;
  pthread_sigmask(SIG_BLOCK, NULL, &caller_sigmask);

  OSThread* osthread = thread->osthread();
  osthread->set_caller_sigmask(caller_sigmask);

  pthread_sigmask(SIG_UNBLOCK, os::Haiku::unblocked_signals(), NULL);

  if (!ReduceSignalUsage) {
    if (thread->is_VM_thread()) {
      // Only the VM thread handles BREAK_SIGNAL ...
      pthread_sigmask(SIG_UNBLOCK, vm_signals(), NULL);
    } else {
      // ... all other threads block BREAK_SIGNAL
      pthread_sigmask(SIG_BLOCK, vm_signals(), NULL);
    }
  }
}


//////////////////////////////////////////////////////////////////////////////
// create new thread

// Thread start routine for all newly created threads
static void *thread_native_entry(Thread *thread) {
  // Try to randomize the cache line index of hot stack frames.
  // This helps when threads of the same stack traces evict each other's
  // cache lines. The threads can be either from the same JVM instance, or
  // from different JVM instances. The benefit is especially true for
  // processors with hyperthreading technology.
  static int counter = 0;
  int pid = os::current_process_id();
  alloca(((pid ^ counter++) & 7) * 128);

  thread->initialize_thread_current();

  OSThread* osthread = thread->osthread();
  Monitor* sync = osthread->startThread_lock();

  // thread_id is kernel thread id (similar to Solaris LWP id)
  osthread->set_thread_id(find_thread(NULL));

  // initialize signal mask for this thread
  os::Haiku::hotspot_sigmask(thread);

  // initialize floating point control register
  os::Haiku::init_thread_fpu_state();

  // handshaking with parent thread
  {
    MutexLockerEx ml(sync, Mutex::_no_safepoint_check_flag);

    // notify parent thread
    osthread->set_state(INITIALIZED);
    sync->notify_all();

    // wait until os::start_thread()
    while (osthread->get_state() == INITIALIZED) {
      sync->wait(Mutex::_no_safepoint_check_flag);
    }
  }

  // call one more level start routine
  thread->run();

  log_info(os, thread)("Thread finished (tid: " UINTX_FORMAT ", kernel thread id: " UINTX_FORMAT ").",
    os::current_thread_id(), find_thread(NULL));

  // If a thread has not deleted itself ("delete this") as part of its
  // termination sequence, we have to ensure thread-local-storage is
  // cleared before we actually terminate. No threads should ever be
  // deleted asynchronously with respect to their termination.
  if (Thread::current_or_null_safe() != NULL) {
    assert(Thread::current_or_null_safe() == thread, "current thread is wrong");
    thread->clear_thread_current();
  }

  return 0;
}

bool os::create_thread(Thread* thread, ThreadType thr_type,
                       size_t req_stack_size) {
  assert(thread->osthread() == NULL, "caller responsible");

  // Allocate the OSThread object
  OSThread* osthread = new OSThread(NULL, NULL);
  if (osthread == NULL) {
    return false;
  }

  // set the correct thread state
  osthread->set_thread_type(thr_type);

  // Initial state is ALLOCATED but not INITIALIZED
  osthread->set_state(ALLOCATED);

  thread->set_osthread(osthread);

  // init thread attributes
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  // calculate stack size if it's not specified by caller
  size_t stack_size = os::Posix::get_initial_stack_size(thr_type, req_stack_size);
  pthread_attr_setstacksize(&attr, stack_size);

  pthread_attr_setguardsize(&attr, os::Haiku::default_guard_size(thr_type));

  ThreadState state;

  pthread_t tid;
  int ret = pthread_create(&tid, &attr, (void* (*)(void*)) thread_native_entry, thread);

  char buf[64];
  if (ret == 0) {
    log_info(os, thread)("Thread started (pthread id: " UINTX_FORMAT ", attributes: %s). ",
      (uintx) tid, os::Posix::describe_pthread_attr(buf, sizeof(buf), &attr));
  } else {
    log_warning(os, thread)("Failed to start thread - pthread_create failed (%s) for attributes: %s.",
      os::errno_name(ret), os::Posix::describe_pthread_attr(buf, sizeof(buf), &attr));
  }

  pthread_attr_destroy(&attr);

  if (ret != 0) {
    // Need to clean up stuff we've allocated so far
    thread->set_osthread(NULL);
    delete osthread;
    return false;
  }

  // OSThread::thread_id is the pthread id.
  osthread->set_pthread_id(tid);

  // Wait until child thread is either initialized or aborted
  {
    Monitor* sync_with_child = osthread->startThread_lock();
    MutexLockerEx ml(sync_with_child, Mutex::_no_safepoint_check_flag);
    while ((state = osthread->get_state()) == ALLOCATED) {
      sync_with_child->wait(Mutex::_no_safepoint_check_flag);
    }
  }

  // Aborted due to thread limit being reached
  if (state == ZOMBIE) {
      thread->set_osthread(NULL);
      delete osthread;
      return false;
  }

  // The thread is returned suspended (in state INITIALIZED),
  // and is started higher up in the call chain
  assert(state == INITIALIZED, "race condition");
  return true;
}

/////////////////////////////////////////////////////////////////////////////
// attach existing thread

// bootstrap the main thread
bool os::create_main_thread(JavaThread* thread) {
  assert(os::Haiku::_main_thread == pthread_self(), "should be called inside main thread");
  return create_attached_thread(thread);
}

bool os::create_attached_thread(JavaThread* thread) {
#ifdef ASSERT
    thread->verify_not_published();
#endif

  // Allocate the OSThread object
  OSThread* osthread = new OSThread(NULL, NULL);

  if (osthread == NULL) {
    return false;
  }

  // Store pthread info into the OSThread
  osthread->set_thread_id(find_thread(NULL));
  osthread->set_pthread_id(::pthread_self());

  // initialize floating point control register
  os::Haiku::init_thread_fpu_state();

  // Initial thread state is RUNNABLE
  osthread->set_state(RUNNABLE);

  thread->set_osthread(osthread);

  // initialize signal mask for this thread
  // and save the caller's signal mask
  os::Haiku::hotspot_sigmask(thread);

  return true;
}

void os::pd_start_thread(Thread* thread) {
  OSThread * osthread = thread->osthread();
  assert(osthread->get_state() != INITIALIZED, "just checking");
  Monitor* sync_with_child = osthread->startThread_lock();
  MutexLockerEx ml(sync_with_child, Mutex::_no_safepoint_check_flag);
  sync_with_child->notify();
}

// Free Haiku resources related to the OSThread
void os::free_thread(OSThread* osthread) {
  assert(osthread != NULL, "osthread not set");

  if (Thread::current()->osthread() == osthread) {
    // Restore caller's signal mask
    sigset_t sigmask = osthread->caller_sigmask();
    pthread_sigmask(SIG_SETMASK, &sigmask, NULL);
   }

  delete osthread;
}

//////////////////////////////////////////////////////////////////////////////
// primordial thread

// Check if current thread is the primordial thread, similar to Solaris thr_main.
bool os::is_primordial_thread(void) {
  return find_thread(NULL) == getpid();
}

////////////////////////////////////////////////////////////////////////////////
// time support

// Time since start-up in seconds to a fine granularity.
// Used by VMSelfDestructTimer and the MemProfiler.
double os::elapsedTime() {
  return ((double)os::elapsed_counter()) / os::elapsed_frequency();
}

jlong os::elapsed_counter() {
  return javaTimeNanos() - initial_time_count;
}

jlong os::elapsed_frequency() {
  return NANOSECS_PER_SEC;
}

bool os::supports_vtime() { return true; }
bool os::enable_vtime()   { return false; }
bool os::vtime_enabled()  { return false; }

double os::elapsedVTime() {
  thread_info info;
  status_t result = get_thread_info(find_thread(NULL), &info);
  assert(result == B_OK, "get_thread_info failed");
  return (info.user_time + info.kernel_time) / SEC_IN_MICROSECS;
}

jlong os::javaTimeMillis() {
  timeval time;
  int status = gettimeofday(&time, NULL);
  assert(status != -1, "haiku error");
  return jlong(time.tv_sec) * 1000  +  jlong(time.tv_usec / 1000);
}

void os::javaTimeSystemUTC(jlong &seconds, jlong &nanos) {
  timeval time;
  int status = gettimeofday(&time, NULL);
  assert(status != -1, "bsd error");
  seconds = jlong(time.tv_sec);
  nanos = jlong(time.tv_usec) * 1000;
}

jlong os::javaTimeNanos() {
  // nano frequency on x86, micro otherwise
  return system_time_nsecs();
}

void os::javaTimeNanos_info(jvmtiTimerInfo *info_ptr) {
  info_ptr->max_value = ALL_64_BITS;

  // CLOCK_MONOTONIC - amount of time since some arbitrary point in the past
  info_ptr->may_skip_backward = false;      // not subject to resetting or drifting
  info_ptr->may_skip_forward = false;       // not subject to resetting or drifting
  info_ptr->kind = JVMTI_TIMER_ELAPSED;     // elapsed not CPU time
}

// Return the real, user, and system times in seconds from an
// arbitrary fixed point in the past.
bool os::getTimesSecs(double* process_real_time,
                      double* process_user_time,
                      double* process_system_time) {
  struct tms ticks;
  clock_t real_ticks = times(&ticks);

  if (real_ticks == (clock_t) (-1)) {
    return false;
  } else {
    double ticks_per_second = (double) clock_tics_per_sec;
    *process_user_time = ((double) ticks.tms_utime) / ticks_per_second;
    *process_system_time = ((double) ticks.tms_stime) / ticks_per_second;
    *process_real_time = ((double) real_ticks) / ticks_per_second;

    return true;
  }
}


char * os::local_time_string(char *buf, size_t buflen) {
  struct tm t;
  time_t long_time;
  time(&long_time);
  localtime_r(&long_time, &t);
  jio_snprintf(buf, buflen, "%d-%02d-%02d %02d:%02d:%02d",
               t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
               t.tm_hour, t.tm_min, t.tm_sec);
  return buf;
}

struct tm* os::localtime_pd(const time_t* clock, struct tm*  res) {
  return localtime_r(clock, res);
}

////////////////////////////////////////////////////////////////////////////////
// runtime exit support

// Note: os::shutdown() might be called very early during initialization, or
// called from signal handler. Before adding something to os::shutdown(), make
// sure it is async-safe and can handle partially initialized VM.
void os::shutdown() {

  // allow PerfMemory to attempt cleanup of any persistent resources
  perfMemory_exit();

  // needs to remove object in file system
  AttachListener::abort();

  // flush buffered output, finish log files
  ostream_abort();

  // Check for abort hook
  abort_hook_t abort_hook = Arguments::abort_hook();
  if (abort_hook != NULL) {
    abort_hook();
  }

}

// Note: os::abort() might be called very early during initialization, or
// called from signal handler. Before adding something to os::abort(), make
// sure it is async-safe and can handle partially initialized VM.
void os::abort(bool dump_core, void* siginfo, const void* context) {
  os::shutdown();
  if (dump_core) {
#ifndef PRODUCT
    fdStream out(defaultStream::output_fd());
    out.print_raw("Current thread is ");
    char buf[16];
    jio_snprintf(buf, sizeof(buf), UINTX_FORMAT, os::current_thread_id());
    out.print_raw_cr(buf);
    out.print_raw_cr("Dumping core ...");
#endif
    ::abort(); // dump core
  }

  ::exit(1);
}

// Die immediately, no exit hook, no abort hook, no cleanup.
void os::die() {
  ::abort();
}


// This method is a copy of JDK's sysGetLastErrorString
// from src/solaris/hpi/src/system_md.c

size_t os::lasterror(char *buf, size_t len) {

  if (errno == 0)  return 0;

  const char *s = ::strerror(errno);
  size_t n = ::strlen(s);
  if (n >= len) {
    n = len - 1;
  }
  ::strncpy(buf, s, n);
  buf[n] = '\0';
  return n;
}

intx os::current_thread_id() { return (intx)pthread_self(); }
int os::current_process_id() {
  return getpid();
}

// DLL functions

const char* os::dll_file_extension() { return ".so"; }

// This must be hard coded because it's the system's temporary
// directory not the java application's temp directory, ala java.io.tmpdir.
const char* os::get_temp_directory() { return "/tmp"; }

// check if addr is inside libjvm.so
bool os::address_is_in_vm(address addr) {
  static address libjvm_base_addr;
  Dl_info dlinfo;

  if (libjvm_base_addr == NULL) {
    if (dladdr(CAST_FROM_FN_PTR(void *, os::address_is_in_vm), &dlinfo) != 0) {
      libjvm_base_addr = (address)dlinfo.dli_fbase;
    }
    assert(libjvm_base_addr !=NULL, "Cannot obtain base address for libjvm");
  }

  if (dladdr((void *)addr, &dlinfo) != 0) {
    if (libjvm_base_addr == (address)dlinfo.dli_fbase) return true;
  }

  return false;
}

bool os::dll_address_to_function_name(address addr, char *buf,
                                      int buflen, int *offset,
                                      bool demangle) {
  // buf is not optional, but offset is optional
  assert(buf != NULL, "sanity check");

  Dl_info dlinfo;

  if (dladdr((void*)addr, &dlinfo) != 0) {
    // see if we have a matching symbol
    if (dlinfo.dli_saddr != NULL && dlinfo.dli_sname != NULL) {
      if (!(demangle && Decoder::demangle(dlinfo.dli_sname, buf, buflen))) {
        jio_snprintf(buf, buflen, "%s", dlinfo.dli_sname);
      }
      if (offset != NULL) *offset = addr - (address)dlinfo.dli_saddr;
      return true;
    }
    // no matching symbol so try for just file info
    if (dlinfo.dli_fname != NULL && dlinfo.dli_fbase != NULL) {
      if (Decoder::decode((address)(addr - (address)dlinfo.dli_fbase),
                          buf, buflen, offset, dlinfo.dli_fname, demangle)) {
        return true;
      }
    }
  }

  buf[0] = '\0';
  if (offset != NULL) *offset = -1;
  return false;
}

bool os::dll_address_to_library_name(address addr, char* buf,
                                     int buflen, int* offset) {
  // buf is not optional, but offset is optional
  assert(buf != NULL, "sanity check");

  Dl_info dlinfo;

  if (dladdr((void*)addr, &dlinfo) != 0) {
    if (dlinfo.dli_fname != NULL) {
      jio_snprintf(buf, buflen, "%s", dlinfo.dli_fname);
    }
    if (dlinfo.dli_fbase != NULL && offset != NULL) {
      *offset = addr - (address)dlinfo.dli_fbase;
    }
    return true;
  }

  buf[0] = '\0';
  if (offset) *offset = -1;
  return false;
}

  // Loads .dll/.so and
  // in case of error it checks if .dll/.so was built for the
  // same architecture as Hotspot is running on

void * os::dll_load(const char *filename, char *ebuf, int ebuflen)
{
  void * result= ::dlopen(filename, RTLD_LAZY);
  if (result != NULL) {
    // Successful loading
    return result;
  }

  Elf32_Ehdr elf_head;

  // Read system error message into ebuf
  // It may or may not be overwritten below
  ::strncpy(ebuf, ::dlerror(), ebuflen-1);
  ebuf[ebuflen-1]='\0';
  int diag_msg_max_length=ebuflen-strlen(ebuf);
  char* diag_msg_buf=ebuf+strlen(ebuf);

  if (diag_msg_max_length==0) {
    // No more space in ebuf for additional diagnostics message
    return NULL;
  }


  int file_descriptor= ::open(filename, O_RDONLY | O_NONBLOCK);

  if (file_descriptor < 0) {
    // Can't open library, report dlerror() message
    return NULL;
  }

  bool failed_to_read_elf_head=
    (sizeof(elf_head)!=
        (::read(file_descriptor, &elf_head,sizeof(elf_head)))) ;

  ::close(file_descriptor);
  if (failed_to_read_elf_head) {
    // file i/o error - report dlerror() msg
    return NULL;
  }

  typedef struct {
    Elf32_Half  code;         // Actual value as defined in elf.h
    Elf32_Half  compat_class; // Compatibility of archs at VM's sense
    char        elf_class;    // 32 or 64 bit
    char        endianess;    // MSB or LSB
    char*       name;         // String representation
  } arch_t;

  #ifndef EM_486
  #define EM_486          6               /* Intel 80486 */
  #endif

  static const arch_t arch_array[]={
    {EM_386,         EM_386,     ELFCLASS32, ELFDATA2LSB, (char*)"IA 32"},
    {EM_486,         EM_386,     ELFCLASS32, ELFDATA2LSB, (char*)"IA 32"},
    {EM_IA_64,       EM_IA_64,   ELFCLASS64, ELFDATA2LSB, (char*)"IA 64"},
    {EM_X86_64,      EM_X86_64,  ELFCLASS64, ELFDATA2LSB, (char*)"AMD 64"},
    {EM_SPARC,       EM_SPARC,   ELFCLASS32, ELFDATA2MSB, (char*)"Sparc 32"},
    {EM_SPARC32PLUS, EM_SPARC,   ELFCLASS32, ELFDATA2MSB, (char*)"Sparc 32"},
    {EM_SPARCV9,     EM_SPARCV9, ELFCLASS64, ELFDATA2MSB, (char*)"Sparc v9 64"},
    {EM_PPC,         EM_PPC,     ELFCLASS32, ELFDATA2MSB, (char*)"Power PC 32"},
    {EM_PPC64,       EM_PPC64,   ELFCLASS64, ELFDATA2MSB, (char*)"Power PC 64"},
    {EM_ARM,         EM_ARM,     ELFCLASS32,   ELFDATA2LSB, (char*)"ARM"},
    {EM_S390,        EM_S390,    ELFCLASSNONE, ELFDATA2MSB, (char*)"IBM System/390"},
    {EM_ALPHA,       EM_ALPHA,   ELFCLASS64, ELFDATA2LSB, (char*)"Alpha"},
    {EM_MIPS_RS3_LE, EM_MIPS_RS3_LE, ELFCLASS32, ELFDATA2LSB, (char*)"MIPSel"},
    {EM_MIPS,        EM_MIPS,    ELFCLASS32, ELFDATA2MSB, (char*)"MIPS"},
    {EM_PARISC,      EM_PARISC,  ELFCLASS32, ELFDATA2MSB, (char*)"PARISC"},
    {EM_68K,         EM_68K,     ELFCLASS32, ELFDATA2MSB, (char*)"M68k"}
  };

  #if  (defined IA32)
    static  Elf32_Half running_arch_code=EM_386;
  #elif   (defined AMD64)
    static  Elf32_Half running_arch_code=EM_X86_64;
  #elif  (defined IA64)
    static  Elf32_Half running_arch_code=EM_IA_64;
  #elif  (defined __sparc) && (defined _LP64)
    static  Elf32_Half running_arch_code=EM_SPARCV9;
  #elif  (defined __sparc) && (!defined _LP64)
    static  Elf32_Half running_arch_code=EM_SPARC;
  #elif  (defined __powerpc64__)
    static  Elf32_Half running_arch_code=EM_PPC64;
  #elif  (defined __powerpc__)
    static  Elf32_Half running_arch_code=EM_PPC;
  #elif  (defined ARM)
    static  Elf32_Half running_arch_code=EM_ARM;
  #elif  (defined S390)
    static  Elf32_Half running_arch_code=EM_S390;
  #elif  (defined ALPHA)
    static  Elf32_Half running_arch_code=EM_ALPHA;
  #elif  (defined MIPSEL)
    static  Elf32_Half running_arch_code=EM_MIPS_RS3_LE;
  #elif  (defined PARISC)
    static  Elf32_Half running_arch_code=EM_PARISC;
  #elif  (defined MIPS)
    static  Elf32_Half running_arch_code=EM_MIPS;
  #elif  (defined M68K)
    static  Elf32_Half running_arch_code=EM_68K;
  #else
    #error Method os::dll_load requires that one of following is defined:\
         IA32, AMD64, IA64, __sparc, __powerpc__, ARM, S390, ALPHA, MIPS, MIPSEL, PARISC, M68K
  #endif

  // Identify compatability class for VM's architecture and library's architecture
  // Obtain string descriptions for architectures

  arch_t lib_arch={elf_head.e_machine,0,elf_head.e_ident[EI_CLASS], elf_head.e_ident[EI_DATA], NULL};
  int running_arch_index=-1;

  for (unsigned int i=0 ; i < ARRAY_SIZE(arch_array) ; i++ ) {
    if (running_arch_code == arch_array[i].code) {
      running_arch_index    = i;
    }
    if (lib_arch.code == arch_array[i].code) {
      lib_arch.compat_class = arch_array[i].compat_class;
      lib_arch.name         = arch_array[i].name;
    }
  }

  assert(running_arch_index != -1,
    "Didn't find running architecture code (running_arch_code) in arch_array");
  if (running_arch_index == -1) {
    // Even though running architecture detection failed
    // we may still continue with reporting dlerror() message
    return NULL;
  }

  if (lib_arch.endianess != arch_array[running_arch_index].endianess) {
    ::snprintf(diag_msg_buf, diag_msg_max_length-1," (Possible cause: endianness mismatch)");
    return NULL;
  }

#ifndef S390
  if (lib_arch.elf_class != arch_array[running_arch_index].elf_class) {
    ::snprintf(diag_msg_buf, diag_msg_max_length-1," (Possible cause: architecture word width mismatch)");
    return NULL;
  }
#endif // !S390

  if (lib_arch.compat_class != arch_array[running_arch_index].compat_class) {
    if ( lib_arch.name!=NULL ) {
      ::snprintf(diag_msg_buf, diag_msg_max_length-1,
        " (Possible cause: can't load %s-bit .so on a %s-bit platform)",
        lib_arch.name, arch_array[running_arch_index].name);
    } else {
      ::snprintf(diag_msg_buf, diag_msg_max_length-1,
      " (Possible cause: can't load this .so (machine code=0x%x) on a %s-bit platform)",
        lib_arch.code,
        arch_array[running_arch_index].name);
    }
  }
  return NULL;
}


void* os::dll_lookup(void* handle, const char* name) {
  return dlsym(handle, name);
}

void* os::get_default_process_handle() {
  return (void*)::dlopen(NULL, RTLD_LAZY);
}

void os::print_dll_info(outputStream *st) {
  st->print_cr("Dynamic libraries:");

  image_info info;
  int32 cookie = 0;
  while (get_next_image_info(0, &cookie, &info) == B_OK) {
    if (info.type == B_LIBRARY_IMAGE) {
      st->print_cr("%p\t%s", info.text, info.name);
    }
  }
}

int os::get_loaded_modules_info(os::LoadedModulesCallbackFunc callback, void *param) {
  return 0;
}

void os::get_summary_os_info(char* buf, size_t buflen) {
  // These buffers are small because we want this to be brief
  // and not use a lot of stack while generating the hs_err file.
  char os[100];
  strncpy(os, "Haiku", sizeof(os));

  char release[100];
  strncpy(release, "", sizeof(release));
  snprintf(buf, buflen, "%s %s", os, release);
}

void os::print_os_info_brief(outputStream* st) {
  os::Posix::print_uname_info(st);

}

void os::print_os_info(outputStream* st) {
  st->print("OS: Haiku");
  st->cr();

  os::Posix::print_uname_info(st);

  os::Posix::print_rlimit_info(st);

  os::Posix::print_load_average(st);

  os::print_memory_info(st);
}

void os::pd_print_cpu_info(outputStream* st, char* buf, size_t buflen) {
}

void os::get_summary_cpu_info(char* buf, size_t buflen) {
}

void os::print_memory_info(outputStream* st) {
  st->print("Memory:");
  st->print(" %dk page", os::vm_page_size()>>10);

  st->print(", physical " UINT64_FORMAT "k",
            os::physical_memory() >> 10);
  st->print("(" UINT64_FORMAT "k free)",
            os::available_memory() >> 10);
  st->cr();
}

static void print_signal_handler(outputStream* st, int sig,
                                 char* buf, size_t buflen);

void os::print_signal_handlers(outputStream* st, char* buf, size_t buflen) {
  st->print_cr("Signal Handlers:");
  print_signal_handler(st, SIGSEGV, buf, buflen);
  print_signal_handler(st, SIGBUS , buf, buflen);
  print_signal_handler(st, SIGFPE , buf, buflen);
  print_signal_handler(st, SIGPIPE, buf, buflen);
  print_signal_handler(st, SIGXFSZ, buf, buflen);
  print_signal_handler(st, SIGILL , buf, buflen);
  print_signal_handler(st, SR_signum, buf, buflen);
  print_signal_handler(st, SHUTDOWN1_SIGNAL, buf, buflen);
  print_signal_handler(st, SHUTDOWN2_SIGNAL , buf, buflen);
  print_signal_handler(st, SHUTDOWN3_SIGNAL , buf, buflen);
  print_signal_handler(st, BREAK_SIGNAL, buf, buflen);
#if defined(PPC64)
  print_signal_handler(st, SIGTRAP, buf, buflen);
#endif
}

static char saved_jvm_path[MAXPATHLEN] = {0};

// Find the full path to the current module, libjvm.so
void os::jvm_path(char *buf, jint buflen) {
  // Error checking.
  if (buflen < MAXPATHLEN) {
    assert(false, "must use a large-enough buffer");
    buf[0] = '\0';
    return;
  }
  // Lazy resolve the path to current module.
  if (saved_jvm_path[0] != 0) {
    strcpy(buf, saved_jvm_path);
    return;
  }

  char dli_fname[MAXPATHLEN];
  bool ret = dll_address_to_library_name(
                CAST_FROM_FN_PTR(address, os::jvm_path),
                dli_fname, sizeof(dli_fname), NULL);
  assert(ret, "cannot locate libjvm");
  char *rp = NULL;
  if (ret && dli_fname[0] != '\0') {
    rp = realpath(dli_fname, buf);
  }
  if (rp == NULL)
    return;

  if (Arguments::sun_java_launcher_is_altjvm()) {
    // Support for the gamma launcher.  Typical value for buf is
    // "<JAVA_HOME>/jre/lib/<vmtype>/libjvm.so".  If "/jre/lib/" appears at
    // the right place in the string, then assume we are installed in a JDK and
    // we're done.  Otherwise, check for a JAVA_HOME environment variable and fix
    // up the path so it looks like libjvm.so is installed there (append a
    // fake suffix hotspot/libjvm.so).
    const char *p = buf + strlen(buf) - 1;
    for (int count = 0; p > buf && count < 5; ++count) {
      for (--p; p > buf && *p != '/'; --p)
        /* empty */ ;
    }

    if (strncmp(p, "/jre/lib/", 9) != 0) {
      // Look for JAVA_HOME in the environment.
      char* java_home_var = ::getenv("JAVA_HOME");
      if (java_home_var != NULL && java_home_var[0] != 0) {
        char* jrelib_p;
        int len;

        // Check the current module name "libjvm.so".
        p = strrchr(buf, '/');
        assert(strstr(p, "/libjvm") == p, "invalid library name");

        rp = realpath(java_home_var, buf);
        if (rp == NULL)
          return;

        // determine if this is a legacy image or modules image
        // modules image doesn't have "jre" subdirectory
        len = strlen(buf);
        assert(len < buflen, "Ran out of buffer room");
        jrelib_p = buf + len;
        snprintf(jrelib_p, buflen-len, "/jre/lib");
        if (0 != access(buf, F_OK)) {
          snprintf(jrelib_p, buflen-len, "/lib");
        }

        if (0 == access(buf, F_OK)) {
          // Use current module name "libjvm.so"
          len = strlen(buf);
          snprintf(buf + len, buflen-len, "/hotspot/libjvm.so");
        } else {
          // Go back to path of .so
          rp = realpath(dli_fname, buf);
          if (rp == NULL)
            return;
        }
      }
    }
  }

  strncpy(saved_jvm_path, buf, MAXPATHLEN);
}

void os::print_jni_name_prefix_on(outputStream* st, int args_size) {
  // no prefix required, not even "_"
}

void os::print_jni_name_suffix_on(outputStream* st, int args_size) {
  // no suffix required
}

////////////////////////////////////////////////////////////////////////////////
// sun.misc.Signal support

static volatile jint sigint_count = 0;

static void
UserHandler(int sig, void *siginfo, void *context) {
  // 4511530 - sem_post is serialized and handled by the manager thread. When
  // the program is interrupted by Ctrl-C, SIGINT is sent to every thread. We
  // don't want to flood the manager thread with sem_post requests.
  if (sig == SIGINT && Atomic::add(1, &sigint_count) > 1)
      return;

  // Ctrl-C is pressed during error reporting, likely because the error
  // handler fails to abort. Let VM die immediately.
  if (sig == SIGINT && VMError::is_error_reported()) {
     os::die();
  }

  os::signal_notify(sig);
}

void* os::user_handler() {
  return CAST_FROM_FN_PTR(void*, UserHandler);
}

static struct timespec create_semaphore_timespec(unsigned int sec, int nsec) {
  struct timespec ts;
  unpackTime(&ts, false, (sec * NANOSECS_PER_SEC) + nsec);

  return ts;
}

extern "C" {
  typedef void (*sa_handler_t)(int);
  typedef void (*sa_sigaction_t)(int, siginfo_t *, void *);
}

void* os::signal(int signal_number, void* handler) {
  struct sigaction sigAct, oldSigAct;

  sigfillset(&(sigAct.sa_mask));
  sigAct.sa_flags   = SA_RESTART|SA_SIGINFO;
  sigAct.sa_handler = CAST_TO_FN_PTR(sa_handler_t, handler);

  if (sigaction(signal_number, &sigAct, &oldSigAct)) {
    // -1 means registration failed
    return (void *)-1;
  }

  return CAST_FROM_FN_PTR(void*, oldSigAct.sa_handler);
}

void os::signal_raise(int signal_number) {
  ::raise(signal_number);
}

/*
 * The following code is moved from os.cpp for making this
 * code platform specific, which it is by its very nature.
 */

// Will be modified when max signal is changed to be dynamic
int os::sigexitnum_pd() {
  return NSIG;
}

// a counter for each possible signal value
static volatile jint pending_signals[NSIG+1] = { 0 };

// Linux(POSIX) specific hand shaking semaphore.
static sem_t sig_sem;
static PosixSemaphore sr_semaphore;

// Use POSIX implementation of semaphores.

static void jdk_misc_signal_init() {
  // Initialize signal structures
  ::memset((void*)pending_signals, 0, sizeof(pending_signals));

  // Initialize signal semaphore
  ::sem_init(&sig_sem, 0, 0);
}

void os::signal_notify(int sig) {
  Atomic::inc(&pending_signals[sig]);
  ::sem_post(&sig_sem);
}

static int check_pending_signals() {
  Atomic::store(0, &sigint_count);
  for (;;) {
    for (int i = 0; i < NSIG + 1; i++) {
      jint n = pending_signals[i];
      if (n > 0 && n == Atomic::cmpxchg(n - 1, &pending_signals[i], n)) {
        return i;
      }
    }
    JavaThread *thread = JavaThread::current();
    ThreadBlockInVM tbivm(thread);

    bool threadIsSuspended;
    do {
      thread->set_suspend_equivalent();
      // cleared by handle_special_suspend_equivalent_condition() or java_suspend_self()
      ::sem_wait(&sig_sem);

      // were we externally suspended while we were waiting?
      threadIsSuspended = thread->handle_special_suspend_equivalent_condition();
      if (threadIsSuspended) {
        //
        // The semaphore has been incremented, but while we were waiting
        // another thread suspended us. We don't want to continue running
        // while suspended because that would surprise the thread that
        // suspended us.
        //
        ::sem_post(&sig_sem);

        thread->java_suspend_self();
      }
    } while (threadIsSuspended);
  }
}

int os::signal_wait() {
  return check_pending_signals();
}

////////////////////////////////////////////////////////////////////////////////
// Virtual Memory

int os::vm_page_size() {
  // Seems redundant as all get out
  assert(os::Haiku::page_size() != -1, "must call os::init");
  return os::Haiku::page_size();
}

int os::vm_allocation_granularity() {
  assert(os::Haiku::page_size() != -1, "must call os::init");
  return os::Haiku::page_size();
}

static void warn_fail_commit_memory(char* addr, size_t size, bool exec,
                                    int err) {
  warning("INFO: os::commit_memory(" PTR_FORMAT ", " SIZE_FORMAT
          ", %d) failed; error='%s' (errno=%d)", p2i(addr), size, exec,
          os::strerror(err), err);
}

bool os::pd_commit_memory(char* addr, size_t size, bool exec) {
  int prot = exec ? PROT_READ|PROT_WRITE|PROT_EXEC : PROT_READ|PROT_WRITE;
  uintptr_t res = (uintptr_t) ::mmap(addr, size, prot,
                                   MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
  return res != (uintptr_t) MAP_FAILED;
}

bool os::pd_commit_memory(char* addr, size_t size, size_t alignment_hint,
                       bool exec) {
  return pd_commit_memory(addr, size, exec);
}

void os::pd_commit_memory_or_exit(char* addr, size_t size, bool exec,
                                  const char* mesg) {
  assert(mesg != NULL, "mesg must be specified");
  if (!pd_commit_memory(addr, size, exec)) {
    warn_fail_commit_memory(addr, size, exec, errno);
    vm_exit_out_of_memory(size, OOM_MMAP_ERROR, "%s", mesg);
  }
}

void os::pd_commit_memory_or_exit(char* addr, size_t size,
                                  size_t alignment_hint, bool exec,
                                  const char* mesg) {
  // alignment_hint is ignored on this OS
  pd_commit_memory_or_exit(addr, size, exec, mesg);
}

void os::pd_realign_memory(char *addr, size_t bytes, size_t alignment_hint) { }
void os::pd_free_memory(char *addr, size_t bytes, size_t alignment_hint) { }
void os::numa_make_global(char *addr, size_t bytes)    { }
void os::numa_make_local(char *addr, size_t bytes, int lgrp_hint)    { }
bool os::numa_topology_changed()                       { return false; }
size_t os::numa_get_groups_num()                       { return 1; }
int os::numa_get_group_id()                            { return 0; }
size_t os::numa_get_leaf_groups(int *ids, size_t size) {
  if (size > 0) {
    ids[0] = 0;
    return 1;
  }
  return 0;
}

bool os::get_page_info(char *start, page_info* info) {
  return false;
}

char *os::scan_pages(char *start, char* end, page_info* page_expected, page_info* page_found) {
  return end;
}

bool os::pd_uncommit_memory(char* addr, size_t size) {
  uintptr_t res = (uintptr_t) ::mmap(addr, size, PROT_NONE,
                MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
  return res  != (uintptr_t) MAP_FAILED;
}

// If the (growable) stack mapping already extends beyond the point
// where we're going to put our guard pages, truncate the mapping at
// that point by munmap()ping it.  This ensures that when we later
// munmap() the guard pages we don't leave a hole in the stack
// mapping. This only affects the main/initial thread, but guard
// against future OS changes
bool os::pd_create_stack_guard_pages(char* addr, size_t size) {
  return os::commit_memory(addr, size, !ExecMem);
}

// If this is a growable mapping, remove the guard pages entirely by
// munmap()ping them.  If not, just call uncommit_memory(). This only
// affects the main/initial thread, but guard against future OS changes
bool os::remove_stack_guard_pages(char* addr, size_t size) {
  return os::uncommit_memory(addr, size);
}

// If 'fixed' is true, anon_mmap() will attempt to reserve anonymous memory
// at 'requested_addr'. If there are existing memory mappings at the same
// location, however, they will be overwritten. If 'fixed' is false,
// 'requested_addr' is only treated as a hint, the return value may or
// may not start from the requested address. Unlike Linux mmap(), this
// function returns NULL to indicate failure.
static char* anon_mmap(char* requested_addr, size_t bytes, bool fixed) {
  char * addr;
  int flags;

  flags = MAP_PRIVATE | MAP_ANONYMOUS;
  if (fixed) {
    assert((uintptr_t)requested_addr % os::Haiku::page_size() == 0, "unaligned address");
    flags |= MAP_FIXED;
  }

  // Map reserved/uncommitted pages PROT_NONE so we fail early if we
  // touch an uncommitted page. Otherwise, the read/write might
  // succeed if we have enough swap space to back the physical page.
  addr = (char*)::mmap(requested_addr, bytes, PROT_NONE,
                       flags, -1, 0);

  return addr == MAP_FAILED ? NULL : addr;
}

static int anon_munmap(char * addr, size_t size) {
  return ::munmap(addr, size) == 0;
}

char* os::pd_reserve_memory(size_t bytes, char* requested_addr,
                         size_t alignment_hint) {
  return anon_mmap(requested_addr, bytes, (requested_addr != NULL));
}

bool os::pd_release_memory(char* addr, size_t size) {
  return anon_munmap(addr, size);
}

static bool haiku_mprotect(char* addr, size_t size, int prot) {
  // Linux wants the mprotect address argument to be page aligned.
  char* bottom = (char*)align_down((intptr_t)addr, os::Haiku::page_size());

  // According to SUSv3, mprotect() should only be used with mappings
  // established by mmap(), and mmap() always maps whole pages. Unaligned
  // 'addr' likely indicates problem in the VM (e.g. trying to change
  // protection of malloc'ed or statically allocated memory). Check the
  // caller if you hit this assert.
  assert(addr == bottom, "sanity check");

  size = align_up(pointer_delta(addr, bottom, 1) + size, os::Haiku::page_size());
  return ::mprotect(bottom, size, prot) == 0;
}

// Set protections specified
bool os::protect_memory(char* addr, size_t bytes, ProtType prot,
                        bool is_committed) {
  unsigned int p = 0;
  switch (prot) {
  case MEM_PROT_NONE: p = PROT_NONE; break;
  case MEM_PROT_READ: p = PROT_READ; break;
  case MEM_PROT_RW:   p = PROT_READ|PROT_WRITE; break;
  case MEM_PROT_RWX:  p = PROT_READ|PROT_WRITE|PROT_EXEC; break;
  default:
    ShouldNotReachHere();
  }
  // is_committed is unused.
  return haiku_mprotect(addr, bytes, p);
}

bool os::guard_memory(char* addr, size_t size) {
  return haiku_mprotect(addr, size, PROT_NONE);
}

bool os::unguard_memory(char* addr, size_t size) {
  return haiku_mprotect(addr, size, PROT_READ|PROT_WRITE);
}

// No large page support on Haiku

void os::large_page_init() {
}

char* os::reserve_memory_special(size_t bytes, size_t alignment, char* req_addr, bool exec) {
  return NULL;
}

bool os::release_memory_special(char* base, size_t bytes) {
  return false;
}

size_t os::large_page_size() {
  return 0;
}

bool os::can_commit_large_page_memory() {
  return false;
}

bool os::can_execute_large_page_memory() {
  return false;
}

char* os::pd_attempt_reserve_memory_at(size_t bytes, char* requested_addr, int file_desc) {
  assert(file_desc >= 0, "file_desc is not valid");
  char* result = pd_attempt_reserve_memory_at(bytes, requested_addr);
  if (result != NULL) {
    if (replace_existing_mapping_with_file_mapping(result, bytes, file_desc) == NULL) {
      vm_exit_during_initialization(err_msg("Error in mapping Java heap at the given filesystem directory"));
    }
  }
  return result;
}

// Reserve memory at an arbitrary address, only if that area is
// available (and not reserved for something else).

char* os::pd_attempt_reserve_memory_at(size_t bytes, char* requested_addr) {
  assert(bytes % os::vm_page_size() == 0, "reserving unexpected size block");
  assert((uintptr_t)requested_addr % os::vm_page_size() == 0,
    "unaligned address");

  char* addr = anon_mmap(requested_addr, bytes, false);
  if (addr == requested_addr) {
  	return addr;
  }

  int result = anon_munmap(addr, bytes);
  assert(result, "munmap failed");
  return NULL;
}

size_t os::read(int fd, void *buf, unsigned int nBytes) {
  return ::read(fd, buf, nBytes);
}

size_t os::read_at(int fd, void *buf, unsigned int nBytes, jlong offset) {
  return ::pread(fd, buf, nBytes, offset);
}

void os::naked_short_sleep(jlong ms) {
  struct timespec req;

  assert(ms < 1000, "Un-interruptable sleep, short time use only");
  req.tv_sec = 0;
  if (ms > 0) {
    req.tv_nsec = (ms % 1000) * 1000000;
  }
  else {
    req.tv_nsec = 1;
  }

  nanosleep(&req, NULL);

  return;
}

// Sleep forever; naked call to OS-specific sleep; use with CAUTION
void os::infinite_sleep() {
  while (true) {    // sleep forever ...
    ::sleep(100);   // ... 100 seconds at a time
  }
}

// Used to convert frequent JVM_Yield() to nops
bool os::dont_yield() {
  return DontYieldALot;
}

void os::naked_yield() {
  sched_yield();
}

////////////////////////////////////////////////////////////////////////////////
// thread priority support

// Note: Normal Linux applications are run with SCHED_OTHER policy. SCHED_OTHER
// only supports dynamic priority, static priority must be zero. For real-time
// applications, Linux supports SCHED_RR which allows static priority (1-99).
// However, for large multi-threaded applications, SCHED_RR is not only slower
// than SCHED_OTHER, but also very unstable (my volano tests hang hard 4 out
// of 5 runs - Sep 2005).
//
// The following code actually changes the niceness of kernel-thread/LWP. It
// has an assumption that setpriority() only modifies one kernel-thread/LWP,
// not the entire user process, and user level threads are 1:1 mapped to kernel
// threads. It has always been the case, but could change in the future. For
// this reason, the code should not be used as default (ThreadPriorityPolicy=0).
// It is only used when ThreadPriorityPolicy=1 and requires root privilege.

int os::java_to_os_priority[CriticalPriority + 1] = {
   1,              // 0 Entry should never be used

   1,              // 1 MinPriority
   3,              // 2
   5,              // 3

   7,              // 4
  10,              // 5 NormPriority
  15,              // 6

  20,              // 7
  75,              // 8
 100,              // 9 NearMaxPriority

 110,              // 10 MaxPriority
 120,              // 11 CriticalPriority
};

OSReturn os::set_native_priority(Thread* thread, int newpri) {
  if ( !UseThreadPriorities || ThreadPriorityPolicy == 0 ) return OS_OK;

  int ret = set_thread_priority(thread->osthread()->thread_id(), newpri);
  return (ret == B_OK) ? OS_OK : OS_ERR;
}

OSReturn os::get_native_priority(const Thread* const thread, int *priority_ptr) {
  if ( !UseThreadPriorities || ThreadPriorityPolicy == 0 ) {
    *priority_ptr = java_to_os_priority[NormPriority];
    return OS_OK;
  }

  thread_info ti;
  if (get_thread_info(thread->osthread()->thread_id(), &ti) == B_OK) {
    *priority_ptr = ti.priority;
    return OS_OK;
  } else {
    return OS_ERR;
  }
}

// Hint to the underlying OS that a task switch would not be good.
// Void return because it's a hint and can fail.
void os::hint_no_preempt() {}

////////////////////////////////////////////////////////////////////////////////
// suspend/resume support

//  the low-level signal-based suspend/resume support is a remnant from the
//  old VM-suspension that used to be for java-suspension, safepoints etc,
//  within hotspot. Now there is a single use-case for this:
//    - calling get_thread_pc() on the VMThread by the flat-profiler task
//      that runs in the watcher thread.
//  The remaining code is greatly simplified from the more general suspension
//  code that used to be used.
//
//  The protocol is quite simple:
//  - suspend:
//      - sends a signal to the target thread
//      - polls the suspend state of the osthread using a yield loop
//      - target thread signal handler (SR_handler) sets suspend state
//        and blocks in sigsuspend until continued
//  - resume:
//      - sets target osthread state to continue
//      - sends signal to end the sigsuspend loop in the SR_handler
//
//  Note that the SR_lock plays no role in this suspend/resume protocol.
//

static void resume_clear_context(OSThread *osthread) {
  osthread->set_ucontext(NULL);
  osthread->set_siginfo(NULL);
}

static void suspend_save_context(OSThread *osthread, siginfo_t* siginfo, ucontext_t* context) {
  osthread->set_ucontext(context);
  osthread->set_siginfo(siginfo);
}

//
// Handler function invoked when a thread's execution is suspended or
// resumed. We have to be careful that only async-safe functions are
// called here (Note: most pthread functions are not async safe and
// should be avoided.)
//
// Note: sigwait() is a more natural fit than sigsuspend() from an
// interface point of view, but sigwait() prevents the signal hander
// from being run. libpthread would get very confused by not having
// its signal handlers run and prevents sigwait()'s use with the
// mutex granting granting signal.
//
// Currently only ever called on the VMThread and JavaThreads (PC sampling)
//
static void SR_handler(int sig, siginfo_t* siginfo, ucontext_t* context) {
  // Save and restore errno to avoid confusing native code with EINTR
  // after sigsuspend.
  int old_errno = errno;

  Thread* thread = Thread::current();
  OSThread* osthread = thread->osthread();
  assert(thread->is_VM_thread() || thread->is_Java_thread(), "Must be VMThread or JavaThread");

  os::SuspendResume::State current = osthread->sr.state();
  if (current == os::SuspendResume::SR_SUSPEND_REQUEST) {
    suspend_save_context(osthread, siginfo, context);

    // attempt to switch the state, we assume we had a SUSPEND_REQUEST
    os::SuspendResume::State state = osthread->sr.suspended();
    if (state == os::SuspendResume::SR_SUSPENDED) {
      sigset_t suspend_set;  // signals for sigsuspend()

      // get current set of blocked signals and unblock resume signal
      pthread_sigmask(SIG_BLOCK, NULL, &suspend_set);
      sigdelset(&suspend_set, SR_signum);

      sr_semaphore.signal();
      // wait here until we are resumed
      while (1) {
        sigsuspend(&suspend_set);

        os::SuspendResume::State result = osthread->sr.running();
        if (result == os::SuspendResume::SR_RUNNING) {
          sr_semaphore.signal();
          break;
        }
      }

    } else if (state == os::SuspendResume::SR_RUNNING) {
      // request was cancelled, continue
    } else {
      ShouldNotReachHere();
    }

    resume_clear_context(osthread);
  } else if (current == os::SuspendResume::SR_RUNNING) {
    // request was cancelled, continue
  } else if (current == os::SuspendResume::SR_WAKEUP_REQUEST) {
    // ignore
  } else {
    // ignore
  }

  errno = old_errno;
}


static int SR_initialize() {
  struct sigaction act;
  char *s;
  /* Get signal number to use for suspend/resume */
  if ((s = ::getenv("_JAVA_SR_SIGNUM")) != 0) {
    int sig = ::strtol(s, 0, 10);
    if (sig > 0 || sig < __MAX_SIGNO) {
        SR_signum = sig;
    }
  }

  sigemptyset(&SR_sigset);
  sigaddset(&SR_sigset, SR_signum);

  /* Set up signal handler for suspend/resume */
  act.sa_flags = SA_RESTART|SA_SIGINFO;
  act.sa_handler = (void (*)(int)) SR_handler;

  // SR_signum is blocked by default.
  // 4528190 - We also need to block pthread restart signal (32 on all
  // supported Linux platforms). Note that LinuxThreads need to block
  // this signal for all threads to work properly. So we don't have
  // to use hard-coded signal number when setting up the mask.
  pthread_sigmask(SIG_BLOCK, NULL, &act.sa_mask);

  if (sigaction(SR_signum, &act, 0) == -1) {
    return -1;
  }

  // Save signal flag
  os::Haiku::set_our_sigflags(SR_signum, act.sa_flags);
  return 0;
}

static int sr_notify(OSThread* osthread) {
  int status = pthread_kill(osthread->pthread_id(), SR_signum);
  assert_status(status == 0, status, "pthread_kill");
  return status;
}

// "Randomly" selected value for how long we want to spin
// before bailing out on suspending a thread, also how often
// we send a signal to a thread we want to resume
static const int RANDOMLY_LARGE_INTEGER = 1000000;
static const int RANDOMLY_LARGE_INTEGER2 = 100;

// returns true on success and false on error - really an error is fatal
// but this seems the normal response to library errors
static bool do_suspend(OSThread* osthread) {
  assert(osthread->sr.is_running(), "thread should be running");
  assert(!sr_semaphore.trywait(), "semaphore has invalid state");

  // mark as suspended and send signal
  if (osthread->sr.request_suspend() != os::SuspendResume::SR_SUSPEND_REQUEST) {
    // failed to switch, state wasn't running?
    ShouldNotReachHere();
    return false;
  }

  if (sr_notify(osthread) != 0) {
    ShouldNotReachHere();
  }

  // managed to send the signal and switch to SUSPEND_REQUEST, now wait for SUSPENDED
  while (true) {
    if (sr_semaphore.timedwait(create_semaphore_timespec(0, 2 * NANOSECS_PER_MILLISEC))) {
      break;
    } else {
      // timeout
      os::SuspendResume::State cancelled = osthread->sr.cancel_suspend();
      if (cancelled == os::SuspendResume::SR_RUNNING) {
        return false;
      } else if (cancelled == os::SuspendResume::SR_SUSPENDED) {
        // make sure that we consume the signal on the semaphore as well
        sr_semaphore.wait();
        break;
      } else {
        ShouldNotReachHere();
        return false;
      }
    }
  }

  guarantee(osthread->sr.is_suspended(), "Must be suspended");
  return true;
}

static void do_resume(OSThread* osthread) {
  assert(osthread->sr.is_suspended(), "thread should be suspended");
  assert(!sr_semaphore.trywait(), "invalid semaphore state");

  if (osthread->sr.request_wakeup() != os::SuspendResume::SR_WAKEUP_REQUEST) {
    // failed to switch to WAKEUP_REQUEST
    ShouldNotReachHere();
    return;
  }

  while (true) {
    if (sr_notify(osthread) == 0) {
      if (sr_semaphore.timedwait(create_semaphore_timespec(0, 2 * NANOSECS_PER_MILLISEC))) {
        if (osthread->sr.is_running()) {
          return;
        }
      }
    } else {
      ShouldNotReachHere();
    }
  }

  guarantee(osthread->sr.is_running(), "Must be running!");
}

///////////////////////////////////////////////////////////////////////////////////
// signal handling (except suspend/resume)

// This routine may be used by user applications as a "hook" to catch signals.
// The user-defined signal handler must pass unrecognized signals to this
// routine, and if it returns true (non-zero), then the signal handler must
// return immediately.  If the flag "abort_if_unrecognized" is true, then this
// routine will never retun false (zero), but instead will execute a VM panic
// routine kill the process.
//
// If this routine returns false, it is OK to call it again.  This allows
// the user-defined signal handler to perform checks either before or after
// the VM performs its own checks.  Naturally, the user code would be making
// a serious error if it tried to handle an exception (such as a null check
// or breakpoint) that the VM was generating for its own correct operation.
//
// This routine may recognize any of the following kinds of signals:
//    SIGBUS, SIGSEGV, SIGILL, SIGFPE, SIGQUIT, SIGPIPE, SIGXFSZ, SIGUSR1.
// It should be consulted by handlers for any of those signals.
//
// The caller of this routine must pass in the three arguments supplied
// to the function referred to in the "sa_sigaction" (not the "sa_handler")
// field of the structure passed to sigaction().  This routine assumes that
// the sa_flags field passed to sigaction() includes SA_SIGINFO and SA_RESTART.
//
// Note that the VM will print warnings if it detects conflicting signal
// handlers, unless invoked with the option "-XX:+AllowUserSignalHandlers".
//
extern "C" JNIEXPORT int
JVM_handle_haiku_signal(int signo, siginfo_t* siginfo,
                        void* ucontext, int abort_if_unrecognized);

static void signalHandler(int sig, siginfo_t* info, void* uc) {
  assert(info != NULL && uc != NULL, "it must be old kernel");
  int orig_errno = errno;  // Preserve errno value over signal handler.
  JVM_handle_haiku_signal(sig, info, uc, true);
  errno = orig_errno;
}


// This boolean allows users to forward their own non-matching signals
// to JVM_handle_haiku_signal, harmlessly.
bool os::Haiku::signal_handlers_are_installed = false;

// For signal-chaining
struct sigaction os::Haiku::sigact[MAXSIGNUM];
unsigned int os::Haiku::sigs = 0;
bool os::Haiku::libjsig_is_loaded = false;
typedef struct sigaction *(*get_signal_t)(int);
get_signal_t os::Haiku::get_signal_action = NULL;

struct sigaction* os::Haiku::get_chained_signal_action(int sig) {
  struct sigaction *actp = NULL;

  if (libjsig_is_loaded) {
    // Retrieve the old signal handler from libjsig
    actp = (*get_signal_action)(sig);
  }
  if (actp == NULL) {
    // Retrieve the preinstalled signal handler from jvm
    actp = get_preinstalled_handler(sig);
  }

  return actp;
}

static bool call_chained_handler(struct sigaction *actp, int sig,
                                 siginfo_t *siginfo, void *context) {
  // Call the old signal handler
  if (actp->sa_handler == SIG_DFL) {
    // It's more reasonable to let jvm treat it as an unexpected exception
    // instead of taking the default action.
    return false;
  } else if (actp->sa_handler != SIG_IGN) {
    if ((actp->sa_flags & SA_NODEFER) == 0) {
      // automaticlly block the signal
      sigaddset(&(actp->sa_mask), sig);
    }

    sa_handler_t hand;
    sa_sigaction_t sa;
    bool siginfo_flag_set = (actp->sa_flags & SA_SIGINFO) != 0;
    // retrieve the chained handler
    if (siginfo_flag_set) {
      sa = actp->sa_sigaction;
    } else {
      hand = actp->sa_handler;
    }

    if ((actp->sa_flags & SA_RESETHAND) != 0) {
      actp->sa_handler = SIG_DFL;
    }

    // try to honor the signal mask
    sigset_t oset;
    pthread_sigmask(SIG_SETMASK, &(actp->sa_mask), &oset);

    // call into the chained handler
    if (siginfo_flag_set) {
      (*sa)(sig, siginfo, context);
    } else {
      (*hand)(sig);
    }

    // restore the signal mask
    pthread_sigmask(SIG_SETMASK, &oset, 0);
  }
  // Tell jvm's signal handler the signal is taken care of.
  return true;
}

bool os::Haiku::chained_handler(int sig, siginfo_t* siginfo, void* context) {
  bool chained = false;
  // signal-chaining
  if (UseSignalChaining) {
    struct sigaction *actp = get_chained_signal_action(sig);
    if (actp != NULL) {
      chained = call_chained_handler(actp, sig, siginfo, context);
    }
  }
  return chained;
}

struct sigaction* os::Haiku::get_preinstalled_handler(int sig) {
  if ((( (unsigned int)1 << sig ) & sigs) != 0) {
    return &sigact[sig];
  }
  return NULL;
}

void os::Haiku::save_preinstalled_handler(int sig, struct sigaction& oldAct) {
  assert(sig > 0 && sig < MAXSIGNUM, "vm signal out of expected range");
  sigact[sig] = oldAct;
  sigs |= (unsigned int)1 << sig;
}

// for diagnostic
int os::Haiku::sigflags[MAXSIGNUM];

int os::Haiku::get_our_sigflags(int sig) {
  assert(sig > 0 && sig < MAXSIGNUM, "vm signal out of expected range");
  return sigflags[sig];
}

void os::Haiku::set_our_sigflags(int sig, int flags) {
  assert(sig > 0 && sig < MAXSIGNUM, "vm signal out of expected range");
  sigflags[sig] = flags;
}

void os::Haiku::set_signal_handler(int sig, bool set_installed) {
  // Check for overwrite.
  struct sigaction oldAct;
  sigaction(sig, (struct sigaction*)NULL, &oldAct);

  void* oldhand = oldAct.sa_sigaction
                ? CAST_FROM_FN_PTR(void*,  oldAct.sa_sigaction)
                : CAST_FROM_FN_PTR(void*,  oldAct.sa_handler);
  if (oldhand != CAST_FROM_FN_PTR(void*, SIG_DFL) &&
      oldhand != CAST_FROM_FN_PTR(void*, SIG_IGN) &&
      oldhand != CAST_FROM_FN_PTR(void*, (sa_sigaction_t)signalHandler)) {
    if (AllowUserSignalHandlers || !set_installed) {
      // Do not overwrite; user takes responsibility to forward to us.
      return;
    } else if (UseSignalChaining) {
      // save the old handler in jvm
      save_preinstalled_handler(sig, oldAct);
      // libjsig also interposes the sigaction() call below and saves the
      // old sigaction on it own.
    } else {
      fatal("Encountered unexpected pre-existing sigaction handler "
                    "%#lx for signal %d.", (long)oldhand, sig);
    }
  }

  struct sigaction sigAct;
  sigfillset(&(sigAct.sa_mask));
  sigAct.sa_handler = SIG_DFL;
  if (!set_installed) {
    sigAct.sa_flags = SA_SIGINFO|SA_RESTART;
  } else {
    sigAct.sa_sigaction = signalHandler;
    sigAct.sa_flags = SA_SIGINFO|SA_RESTART;
  }
  // Save flags, which are set by ours
  assert(sig > 0 && sig < MAXSIGNUM, "vm signal out of expected range");
  sigflags[sig] = sigAct.sa_flags;

  int ret = sigaction(sig, &sigAct, &oldAct);
  assert(ret == 0, "check");

  void* oldhand2  = oldAct.sa_sigaction
                  ? CAST_FROM_FN_PTR(void*, oldAct.sa_sigaction)
                  : CAST_FROM_FN_PTR(void*, oldAct.sa_handler);
  assert(oldhand2 == oldhand, "no concurrent signal handler installation");
}

// install signal handlers for signals that HotSpot needs to
// handle in order to support Java-level exception handling.

void os::Haiku::install_signal_handlers() {
  if (!signal_handlers_are_installed) {
    signal_handlers_are_installed = true;

    // signal-chaining
    typedef void (*signal_setting_t)();
    signal_setting_t begin_signal_setting = NULL;
    signal_setting_t end_signal_setting = NULL;
    begin_signal_setting = CAST_TO_FN_PTR(signal_setting_t,
                             dlsym(RTLD_DEFAULT, "JVM_begin_signal_setting"));
    if (begin_signal_setting != NULL) {
      end_signal_setting = CAST_TO_FN_PTR(signal_setting_t,
                             dlsym(RTLD_DEFAULT, "JVM_end_signal_setting"));
      get_signal_action = CAST_TO_FN_PTR(get_signal_t,
                            dlsym(RTLD_DEFAULT, "JVM_get_signal_action"));
      libjsig_is_loaded = true;
      assert(UseSignalChaining, "should enable signal-chaining");
    }
    if (libjsig_is_loaded) {
      // Tell libjsig jvm is setting signal handlers
      (*begin_signal_setting)();
    }

    set_signal_handler(SIGSEGV, true);
    set_signal_handler(SIGPIPE, true);
    set_signal_handler(SIGBUS, true);
    set_signal_handler(SIGILL, true);
    set_signal_handler(SIGFPE, true);
#if defined(PPC64)
    set_signal_handler(SIGTRAP, true);
#endif
    set_signal_handler(SIGXFSZ, true);

    if (libjsig_is_loaded) {
      // Tell libjsig jvm finishes setting signal handlers
      (*end_signal_setting)();
    }

    // We don't activate signal checker if libjsig is in place, we trust ourselves
    // and if UserSignalHandler is installed all bets are off.
    // Log that signal checking is off only if -verbose:jni is specified.
    if (CheckJNICalls) {
      if (libjsig_is_loaded) {
        if (PrintJNIResolving) {
          tty->print_cr("Info: libjsig is activated, all active signal checking is disabled");
        }
        check_signals = false;
      }
      if (AllowUserSignalHandlers) {
        if (PrintJNIResolving) {
          tty->print_cr("Info: AllowUserSignalHandlers is activated, all active signal checking is disabled");
        }
        check_signals = false;
      }
    }
  }
}

jlong os::Haiku::fast_thread_cpu_time(Thread* thread, bool user_sys_cpu_time) {
  thread_info ti;
  status_t status = get_thread_info(thread->osthread()->thread_id(), &ti);
  assert(status == B_OK, "get_thread_info did not return B_OK");
  return (ti.user_time + user_sys_cpu_time ? ti.kernel_time : 0) * 1000;
}

/////
// glibc on Linux platform uses non-documented flag
// to indicate, that some special sort of signal
// trampoline is used.
// We will never set this flag, and we should
// ignore this flag in our diagnostic
#ifdef SIGNIFICANT_SIGNAL_MASK
#undef SIGNIFICANT_SIGNAL_MASK
#endif
#define SIGNIFICANT_SIGNAL_MASK (~0x04000000)

static const char* get_signal_handler_name(address handler,
                                           char* buf, int buflen) {
  int offset = 0;
  bool found = os::dll_address_to_library_name(handler, buf, buflen, &offset);
  if (found) {
    // skip directory names
    const char *p1, *p2;
    p1 = buf;
    size_t len = strlen(os::file_separator());
    while ((p2 = strstr(p1, os::file_separator())) != NULL) p1 = p2 + len;
    jio_snprintf(buf, buflen, "%s+0x%x", p1, offset);
  } else {
    jio_snprintf(buf, buflen, PTR_FORMAT, handler);
  }
  return buf;
}

static void print_signal_handler(outputStream* st, int sig,
                                 char* buf, size_t buflen) {
  struct sigaction sa;

  sigaction(sig, NULL, &sa);

  // See comment for SIGNIFICANT_SIGNAL_MASK define
  sa.sa_flags &= SIGNIFICANT_SIGNAL_MASK;

  st->print("%s: ", os::exception_name(sig, buf, buflen));

  address handler = (sa.sa_flags & SA_SIGINFO)
    ? CAST_FROM_FN_PTR(address, sa.sa_sigaction)
    : CAST_FROM_FN_PTR(address, sa.sa_handler);

  if (handler == CAST_FROM_FN_PTR(address, SIG_DFL)) {
    st->print("SIG_DFL");
  } else if (handler == CAST_FROM_FN_PTR(address, SIG_IGN)) {
    st->print("SIG_IGN");
  } else {
    st->print("[%s]", get_signal_handler_name(handler, buf, buflen));
  }

  st->print(", sa_mask[0]=");
  os::Posix::print_signal_set_short(st, &sa.sa_mask);

  address rh = VMError::get_resetted_sighandler(sig);
  // May be, handler was resetted by VMError?
  if(rh != NULL) {
    handler = rh;
    sa.sa_flags = VMError::get_resetted_sigflags(sig) & SIGNIFICANT_SIGNAL_MASK;
  }

  st->print(", sa_flags=");
  os::Posix::print_sa_flags(st, sa.sa_flags);

  // Check: is it our handler?
  if(handler == CAST_FROM_FN_PTR(address, (sa_sigaction_t)signalHandler) ||
     handler == CAST_FROM_FN_PTR(address, (sa_sigaction_t)SR_handler)) {
    // It is our signal handler
    // check for flags, reset system-used one!
    if((int)sa.sa_flags != os::Haiku::get_our_sigflags(sig)) {
      st->print(
                ", flags was changed from " PTR32_FORMAT ", consider using jsig library",
                os::Haiku::get_our_sigflags(sig));
    }
  }
  st->cr();
}


#define DO_SIGNAL_CHECK(sig) \
  if (!sigismember(&check_signal_done, sig)) \
    os::Haiku::check_signal_handler(sig)

// This method is a periodic task to check for misbehaving JNI applications
// under CheckJNI, we can add any periodic checks here

void os::run_periodic_checks() {

  if (check_signals == false) return;

  // SEGV and BUS if overridden could potentially prevent
  // generation of hs*.log in the event of a crash, debugging
  // such a case can be very challenging, so we absolutely
  // check the following for a good measure:
  DO_SIGNAL_CHECK(SIGSEGV);
  DO_SIGNAL_CHECK(SIGILL);
  DO_SIGNAL_CHECK(SIGFPE);
  DO_SIGNAL_CHECK(SIGBUS);
  DO_SIGNAL_CHECK(SIGPIPE);
  DO_SIGNAL_CHECK(SIGXFSZ);
#if defined(PPC64)
  DO_SIGNAL_CHECK(SIGTRAP);
#endif

  // ReduceSignalUsage allows the user to override these handlers
  // see comments at the very top and jvm_md.h
  if (!ReduceSignalUsage) {
    DO_SIGNAL_CHECK(SHUTDOWN1_SIGNAL);
    DO_SIGNAL_CHECK(SHUTDOWN2_SIGNAL);
    DO_SIGNAL_CHECK(SHUTDOWN3_SIGNAL);
    DO_SIGNAL_CHECK(BREAK_SIGNAL);
  }

  DO_SIGNAL_CHECK(SR_signum);
}

typedef int (*os_sigaction_t)(int, const struct sigaction *, struct sigaction *);

static os_sigaction_t os_sigaction = NULL;

void os::Haiku::check_signal_handler(int sig) {
  char buf[O_BUFLEN];
  address jvmHandler = NULL;


  struct sigaction act;
  if (os_sigaction == NULL) {
    // only trust the default sigaction, in case it has been interposed
    os_sigaction = (os_sigaction_t)dlsym(RTLD_DEFAULT, "sigaction");
    if (os_sigaction == NULL) return;
  }

  os_sigaction(sig, (struct sigaction*)NULL, &act);


  act.sa_flags &= SIGNIFICANT_SIGNAL_MASK;

  address thisHandler = (act.sa_flags & SA_SIGINFO)
    ? CAST_FROM_FN_PTR(address, act.sa_sigaction)
    : CAST_FROM_FN_PTR(address, act.sa_handler) ;


  switch(sig) {
  case SIGSEGV:
  case SIGBUS:
  case SIGFPE:
  case SIGPIPE:
  case SIGILL:
  case SIGXFSZ:
    jvmHandler = CAST_FROM_FN_PTR(address, (sa_sigaction_t)signalHandler);
    break;

  case SHUTDOWN1_SIGNAL:
  case SHUTDOWN2_SIGNAL:
  case SHUTDOWN3_SIGNAL:
  case BREAK_SIGNAL:
    jvmHandler = (address)user_handler();
    break;

  default:
    if (sig == SR_signum) {
      jvmHandler = CAST_FROM_FN_PTR(address, (sa_sigaction_t)SR_handler);
    } else {
      return;
    }
    break;
  }

  if (thisHandler != jvmHandler) {
    tty->print("Warning: %s handler ", exception_name(sig, buf, O_BUFLEN));
    tty->print("expected:%s", get_signal_handler_name(jvmHandler, buf, O_BUFLEN));
    tty->print_cr("  found:%s", get_signal_handler_name(thisHandler, buf, O_BUFLEN));
    // No need to check this sig any longer
    sigaddset(&check_signal_done, sig);
    // Running under non-interactive shell, SHUTDOWN2_SIGNAL will be reassigned SIG_IGN
    if (sig == SHUTDOWN2_SIGNAL && !isatty(fileno(stdin))) {
      tty->print_cr("Running in non-interactive shell, %s handler is replaced by shell",
                    exception_name(sig, buf, O_BUFLEN));
    }
  } else if(os::Haiku::get_our_sigflags(sig) != 0 && (int)act.sa_flags != os::Haiku::get_our_sigflags(sig)) {
    tty->print("Warning: %s handler flags ", exception_name(sig, buf, O_BUFLEN));
    tty->print("expected:" PTR32_FORMAT, os::Haiku::get_our_sigflags(sig));
    tty->print_cr("  found:" PTR32_FORMAT, act.sa_flags);
    // No need to check this sig any longer
    sigaddset(&check_signal_done, sig);
  }

  // Dump all the signal
  if (sigismember(&check_signal_done, sig)) {
    print_signal_handlers(tty, buf, O_BUFLEN);
  }
}

extern void report_error(char* file_name, int line_no, char* title, char* format, ...);

extern bool signal_name(int signo, char* buf, size_t len);

// this is called _before_ the most of global arguments have been parsed
void os::init(void) {
  clock_tics_per_sec = sysconf(_SC_CLK_TCK);

  init_random(1234567);

  Haiku::set_page_size(sysconf(_SC_PAGESIZE));
  if (Haiku::page_size() == -1) {
    fatal("os_haiku.cpp: os::init: sysconf failed (%s)",
          os::strerror(errno));
  }
  init_page_sizes((size_t) Haiku::page_size());

  Haiku::initialize_system_info();

  // main_thread points to the aboriginal thread
  Haiku::_main_thread = pthread_self();

  initial_time_count = javaTimeNanos();

  os::Posix::init();
}

// To install functions for atexit system call
extern "C" {
  static void perfMemory_exit_helper() {
    perfMemory_exit();
  }
}

// this is called _after_ the global arguments have been parsed
jint os::init_2(void)
{
  os::Posix::init_2();

  // initialize suspend/resume support - must do this before signal_sets_init()
  if (SR_initialize() != 0) {
    perror("SR_initialize failed");
    return JNI_ERR;
  }

  Haiku::signal_sets_init();
  Haiku::install_signal_handlers();
  // Initialize data for jdk.internal.misc.Signal
  if (!ReduceSignalUsage) {
    jdk_misc_signal_init();
  }

  // Check and sets minimum stack sizes against command line options
  if (Posix::set_minimum_stack_sizes() == JNI_ERR) {
    return JNI_ERR;
  }

  if (MaxFDLimit) {
    // set the number of file descriptors to max. print out error
    // if getrlimit/setrlimit fails but continue regardless.
    struct rlimit nbr_files;
    int status = getrlimit(RLIMIT_NOFILE, &nbr_files);
    if (status != 0) {
      if (PrintMiscellaneous && (Verbose || WizardMode))
        perror("os::init_2 getrlimit failed");
    } else {
      nbr_files.rlim_cur = nbr_files.rlim_max;
      status = setrlimit(RLIMIT_NOFILE, &nbr_files);
      if (status != 0) {
        if (PrintMiscellaneous && (Verbose || WizardMode))
          perror("os::init_2 setrlimit failed");
      }
    }
  }

  // at-exit methods are called in the reverse order of their registration.
  // atexit functions are called on return from main or as a result of a
  // call to exit(3C). There can be only 32 of these functions registered
  // and atexit() does not set errno.
  if (PerfAllowAtExitRegistration) {
    // only register atexit functions if PerfAllowAtExitRegistration is set.
    // atexit functions can be delayed until process exit time, which
    // can be problematic for embedded VM situations. Embedded VMs should
    // call DestroyJavaVM() to assure that VM resources are released.

    // note: perfMemory_exit_helper atexit function may be removed in
    // the future if the appropriate cleanup code can be added to the
    // VM_Exit VMOperation's doit method.
    if (atexit(perfMemory_exit_helper) != 0) {
      warning("os::init_2 atexit(perfMemory_exit_helper) failed");
    }
  }

  return JNI_OK;
}

// Mark the polling page as unreadable
void os::make_polling_page_unreadable(void) {
  if (!guard_memory((char*)_polling_page, Haiku::page_size()))
    fatal("Could not disable polling page");
};

// Mark the polling page as readable
void os::make_polling_page_readable(void) {
  if (!haiku_mprotect((char *)_polling_page, Haiku::page_size(), PROT_READ)) {
    fatal("Could not enable polling page");
  }
};

int os::active_processor_count() {
  int online_cpus = ::sysconf(_SC_NPROCESSORS_ONLN);
  assert(online_cpus > 0 && online_cpus <= processor_count(), "sanity check");
  return online_cpus;
}

void os::set_native_thread_name(const char *name) {
  // Not yet implemented.
  return;
}

bool os::distribute_processes(uint length, uint* distribution) {
  // Not yet implemented.
  return false;
}

bool os::bind_to_processor(uint processor_id) {
  // Not yet implemented.
  return false;
}

///

void os::SuspendedThreadTask::internal_do_task() {
  if (do_suspend(_thread->osthread())) {
    SuspendedThreadTaskContext context(_thread, _thread->osthread()->ucontext());
    do_task(context);
    do_resume(_thread->osthread());
  }
}

////////////////////////////////////////////////////////////////////////////////
// debug support

bool os::find(address addr, outputStream* st) {
  Dl_info dlinfo;
  memset(&dlinfo, 0, sizeof(dlinfo));
  if (dladdr(addr, &dlinfo) != 0) {
    st->print(PTR_FORMAT ": ", p2i(addr));
    if (dlinfo.dli_sname != NULL && dlinfo.dli_saddr != NULL) {
      st->print("%s+%#lx", dlinfo.dli_sname,
                 p2i(addr) - p2i(dlinfo.dli_saddr));
    } else if (dlinfo.dli_fbase != NULL) {
      st->print("<offset %#lx>", p2i(addr) - p2i(dlinfo.dli_fbase));
    } else {
      st->print("<absolute address>");
    }
    if (dlinfo.dli_fname != NULL) {
      st->print(" in %s", dlinfo.dli_fname);
    }
    if (dlinfo.dli_fbase != NULL) {
      st->print(" at " PTR_FORMAT, p2i(dlinfo.dli_fbase));
    }
    st->cr();

    if (Verbose) {
      // decode some bytes around the PC
      address begin = clamp_address_in_page(addr-40, addr, os::vm_page_size());
      address end   = clamp_address_in_page(addr+40, addr, os::vm_page_size());
      address       lowest = (address) dlinfo.dli_sname;
      if (!lowest)  lowest = (address) dlinfo.dli_fbase;
      if (begin < lowest)  begin = lowest;
      Dl_info dlinfo2;
      if (dladdr(end, &dlinfo2) != 0 && dlinfo2.dli_saddr != dlinfo.dli_saddr
          && end > dlinfo2.dli_saddr && dlinfo2.dli_saddr > begin)
        end = (address) dlinfo2.dli_saddr;
      Disassembler::decode(begin, end, st);
    }
    return true;
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
// misc

// This does not do anything on Linux. This is basically a hook for being
// able to use structured exception handling (thread-local exception filters)
// on, e.g., Win32.
void
os::os_exception_wrapper(java_call_t f, JavaValue* value, const methodHandle& method,
                         JavaCallArguments* args, Thread* thread) {
  f(value, method, args, thread);
}

void os::print_statistics() {
}

address os::current_stack_base() {
  thread_info ti;
  get_thread_info(find_thread(NULL), &ti);
  return (address)ti.stack_end;
}

size_t os::current_stack_size() {
  thread_info ti;
  get_thread_info(find_thread(NULL), &ti);
  return (intptr_t)ti.stack_end - (intptr_t)ti.stack_base;
}

static inline struct timespec get_mtime(const char* filename) {
  struct stat st;
  int ret = os::stat(filename, &st);
  assert(ret == 0, "failed to stat() file '%s': %s", filename, strerror(errno));
  return st.st_mtim;
}

int os::compare_file_modified_times(const char* file1, const char* file2) {
  struct timespec filetime1 = get_mtime(file1);
  struct timespec filetime2 = get_mtime(file2);
  int diff = filetime1.tv_sec - filetime2.tv_sec;
  if (diff == 0) {
    return filetime1.tv_nsec - filetime2.tv_nsec;
  }
  return diff;
}

bool os::message_box(const char* title, const char* message) {
  int i;
  fdStream err(defaultStream::error_fd());
  for (i = 0; i < 78; i++) err.print_raw("=");
  err.cr();
  err.print_raw_cr(title);
  for (i = 0; i < 78; i++) err.print_raw("-");
  err.cr();
  err.print_raw_cr(message);
  for (i = 0; i < 78; i++) err.print_raw("=");
  err.cr();

  char buf[16];
  // Prevent process from exiting upon "read error" without consuming all CPU
  while (::read(0, buf, sizeof(buf)) <= 0) { ::sleep(100); }

  return buf[0] == 'y' || buf[0] == 'Y';
}

int local_vsnprintf(char* buf, size_t count, const char* format, va_list args) {
  return ::vsnprintf(buf, count, format, args);
}

// Is a (classpath) directory empty?
bool os::dir_is_empty(const char* path) {
  DIR *dir = NULL;
  struct dirent *ptr;

  dir = opendir(path);
  if (dir == NULL) return true;

  /* Scan the directory */
  bool result = true;
  char buf[sizeof(struct dirent) + MAX_PATH];
  while (result && (ptr = ::readdir(dir)) != NULL) {
    if (strcmp(ptr->d_name, ".") != 0 && strcmp(ptr->d_name, "..") != 0) {
      result = false;
    }
  }
  closedir(dir);
  return result;
}

// This code originates from JDK's sysOpen and open64_w
// from src/solaris/hpi/src/system_md.c

#ifndef O_DELETE
#define O_DELETE 0x10000
#endif

// Open a file. Unlink the file immediately after open returns
// if the specified oflag has the O_DELETE flag set.
// O_DELETE is used only in j2se/src/share/native/java/util/zip/ZipFile.c

int os::open(const char *path, int oflag, int mode) {

  if (strlen(path) > MAX_PATH - 1) {
    errno = ENAMETOOLONG;
    return -1;
  }
  int fd;
  int o_delete = (oflag & O_DELETE);
  oflag = oflag & ~O_DELETE;

  fd = ::open(path, oflag, mode);
  if (fd == -1) return -1;

  //If the open succeeded, the file might still be a directory
  {
    struct stat buf;
    int ret = ::fstat(fd, &buf);
    int st_mode = buf.st_mode;

    if (ret != -1) {
      if ((st_mode & S_IFMT) == S_IFDIR) {
        errno = EISDIR;
        ::close(fd);
        return -1;
      }
    } else {
      ::close(fd);
      return -1;
    }
  }

    /*
     * All file descriptors that are opened in the JVM and not
     * specifically destined for a subprocess should have the
     * close-on-exec flag set.  If we don't set it, then careless 3rd
     * party native code might fork and exec without closing all
     * appropriate file descriptors (e.g. as we do in closeDescriptors in
     * UNIXProcess.c), and this in turn might:
     *
     * - cause end-of-file to fail to be detected on some file
     *   descriptors, resulting in mysterious hangs, or
     *
     * - might cause an fopen in the subprocess to fail on a system
     *   suffering from bug 1085341.
     *
     * (Yes, the default setting of the close-on-exec flag is a Unix
     * design flaw)
     *
     * See:
     * 1085341: 32-bit stdio routines should support file descriptors >255
     * 4843136: (process) pipe file descriptor from Runtime.exec not being closed
     * 6339493: (process) Runtime.exec does not close all file descriptors on Solaris 9
     */
#ifdef FD_CLOEXEC
    {
        int flags = ::fcntl(fd, F_GETFD);
        if (flags != -1)
            ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
#endif

  if (o_delete != 0) {
    ::unlink(path);
  }
  return fd;
}


// create binary file, rewriting existing file if required
int os::create_binary_file(const char* path, bool rewrite_existing) {
  int oflags = O_WRONLY | O_CREAT;
  if (!rewrite_existing) {
    oflags |= O_EXCL;
  }
  return ::open(path, oflags, S_IREAD | S_IWRITE);
}

// return current position of file pointer
jlong os::current_file_offset(int fd) {
  return (jlong)::lseek(fd, (off_t)0, SEEK_CUR);
}

// move file pointer to the specified offset
jlong os::seek_to_file_offset(int fd, jlong offset) {
  return (jlong)::lseek(fd, (off_t)offset, SEEK_SET);
}

// This code originates from JDK's sysAvailable
// from src/solaris/hpi/src/native_threads/src/sys_api_td.c

int os::available(int fd, jlong *bytes) {
  jlong cur, end;
  int mode;
  struct stat buf;

  if (::fstat(fd, &buf) >= 0) {
    mode = buf.st_mode;
    if (S_ISCHR(mode) || S_ISFIFO(mode) || S_ISSOCK(mode)) {
      int n;
      if (::ioctl(fd, FIONREAD, &n) >= 0) {
        *bytes = n;
        return 1;
      }
    }
  }
  if ((cur = ::lseek(fd, 0L, SEEK_CUR)) == -1) {
    return 0;
  } else if ((end = ::lseek(fd, 0L, SEEK_END)) == -1) {
    return 0;
  } else if (::lseek(fd, cur, SEEK_SET) == -1) {
    return 0;
  }
  *bytes = end - cur;
  return 1;
}

// Map a block of memory.
char* os::pd_map_memory(int fd, const char* file_name, size_t file_offset,
                     char *addr, size_t bytes, bool read_only,
                     bool allow_exec) {
  int prot;
  int flags = MAP_PRIVATE;

  if (read_only) {
    prot = PROT_READ;
  } else {
    prot = PROT_READ | PROT_WRITE;
  }

  if (allow_exec) {
    prot |= PROT_EXEC;
  }

  if (addr != NULL) {
    flags |= MAP_FIXED;
  }

  char* mapped_address = (char*)mmap(addr, (size_t)bytes, prot, flags,
                                     fd, file_offset);
  if (mapped_address == MAP_FAILED) {
    return NULL;
  }
  return mapped_address;
}


// Remap a block of memory.
char* os::pd_remap_memory(int fd, const char* file_name, size_t file_offset,
                       char *addr, size_t bytes, bool read_only,
                       bool allow_exec) {
  // same as map_memory() on this OS
  return os::map_memory(fd, file_name, file_offset, addr, bytes, read_only,
                        allow_exec);
}


// Unmap a block of memory.
bool os::pd_unmap_memory(char* addr, size_t bytes) {
  return munmap(addr, bytes) == 0;
}

// current_thread_cpu_time(bool) and thread_cpu_time(Thread*, bool)
// are used by JVM M&M and JVMTI to get user+sys or user CPU time
// of a thread.
//
// current_thread_cpu_time() and thread_cpu_time(Thread*) returns
// the fast estimate available on the platform.

jlong os::current_thread_cpu_time() {
  return os::Haiku::fast_thread_cpu_time(Thread::current(), true);
}

jlong os::thread_cpu_time(Thread* thread) {
  return os::Haiku::fast_thread_cpu_time(thread, true);
}

jlong os::current_thread_cpu_time(bool user_sys_cpu_time) {
  return os::Haiku::fast_thread_cpu_time(Thread::current(), user_sys_cpu_time);
}

jlong os::thread_cpu_time(Thread *thread, bool user_sys_cpu_time) {
  return os::Haiku::fast_thread_cpu_time(thread, user_sys_cpu_time);
}

//
//  -1 on error.
//

void os::current_thread_cpu_time_info(jvmtiTimerInfo *info_ptr) {
  info_ptr->max_value = ALL_64_BITS;       // will not wrap in less than 64 bits
  info_ptr->may_skip_backward = false;     // elapsed time not wall time
  info_ptr->may_skip_forward = false;      // elapsed time not wall time
  info_ptr->kind = JVMTI_TIMER_TOTAL_CPU;  // user+system time is returned
}

void os::thread_cpu_time_info(jvmtiTimerInfo *info_ptr) {
  info_ptr->max_value = ALL_64_BITS;       // will not wrap in less than 64 bits
  info_ptr->may_skip_backward = false;     // elapsed time not wall time
  info_ptr->may_skip_forward = false;      // elapsed time not wall time
  info_ptr->kind = JVMTI_TIMER_TOTAL_CPU;  // user+system time is returned
}

bool os::is_thread_cpu_time_supported() {
  return true;
}

// System loadavg support.  Returns -1 if load average cannot be obtained.
// Linux doesn't yet have a (official) notion of processor sets,
// so just return the system wide load average.
int os::loadavg(double loadavg[], int nelem) {
  return -1;
}

void os::pause() {
  char filename[MAX_PATH];
  if (PauseAtStartupFile && PauseAtStartupFile[0]) {
    jio_snprintf(filename, MAX_PATH, PauseAtStartupFile);
  } else {
    jio_snprintf(filename, MAX_PATH, "./vm.paused.%d", current_process_id());
  }

  int fd = ::open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd != -1) {
    struct stat buf;
    ::close(fd);
    while (::stat(filename, &buf) == 0) {
      (void)::poll(NULL, 0, 100);
    }
  } else {
    jio_fprintf(stderr,
      "Could not open pause file '%s', continuing immediately.\n", filename);
  }
}

extern char** environ;

// Run the specified command in a separate process. Return its exit value,
// or -1 on failure (e.g. can't fork a new process).
// Unlike system(), this function can be called from signal handler. It
// doesn't block SIGINT et al.
int os::fork_and_exec(char* cmd) {
  const char * argv[4] = {"sh", "-c", cmd, NULL};

  // fork() in LinuxThreads/NPTL is not async-safe. It needs to run
  // pthread_atfork handlers and reset pthread library. All we need is a
  // separate process to execve. Make a direct syscall to fork process.
  // On IA64 there's no fork syscall, we have to use fork() and hope for
  // the best...
  pid_t pid = fork();

  if (pid < 0) {
    // fork failed
    return -1;

  } else if (pid == 0) {
    // child process

    // execve() in LinuxThreads will call pthread_kill_other_threads_np()
    // first to kill every thread on the thread list. Because this list is
    // not reset by fork() (see notes above), execve() will instead kill
    // every thread in the parent process. We know this is the only thread
    // in the new process, so make a system call directly.
    // IA64 should use normal execve() from glibc to match the glibc fork()
    // above.
    execve("/bin/sh", (char* const*)argv, environ);

    // execve failed
    _exit(-1);

  } else  {
    // copied from J2SE ..._waitForProcessExit() in UNIXProcess_md.c; we don't
    // care about the actual exit code, for now.

    int status;

    // Wait for the child process to exit.  This returns immediately if
    // the child has already exited. */
    while (waitpid(pid, &status, 0) < 0) {
        switch (errno) {
        case ECHILD: return 0;
        case EINTR: break;
        default: return -1;
        }
    }

    if (WIFEXITED(status)) {
       // The child exited normally; get its exit code.
       return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
       // The child exited because of a signal
       // The best value to return is 0x80 + signal number,
       // because that is what all Unix shells do, and because
       // it allows callers to distinguish between process exit and
       // process death by signal.
       return 0x80 + WTERMSIG(status);
    } else {
       // Unknown exit code; pass it through
       return status;
    }
  }
}

// Get the default path to the core file
// Returns the length of the string
int os::get_core_path(char* buffer, size_t bufferSize) {
  const char* p = get_current_directory(buffer, bufferSize);

  if (p == NULL) {
    assert(p != NULL, "failed to get current directory");
    return 0;
  }

  return strlen(buffer);
}

bool os::start_debugging(char *buf, int buflen) {
  int len = (int)strlen(buf);
  char *p = &buf[len];

  jio_snprintf(p, buflen-len,
               "\n\n"
               "Do you want to debug the problem?\n\n"
               "To debug, run 'gdb /proc/%d/exe %d'; then switch to thread " UINTX_FORMAT " (" INTPTR_FORMAT ")\n"
               "Enter 'yes' to launch gdb automatically (PATH must include gdb)\n"
               "Otherwise, press RETURN to abort...",
               os::current_process_id(), os::current_process_id(),
               os::current_thread_id(), os::current_thread_id());

  bool yes = os::message_box("Unexpected Error", buf);

  if (yes) {
    // yes, user asked VM to launch debugger
    jio_snprintf(buf, sizeof(char)*buflen, "gdb /proc/%d/exe %d",
                 os::current_process_id(), os::current_process_id());

    os::fork_and_exec(buf);
    yes = false;
  }
  return yes;
}

// JSR166
// -------------------------------------------------------

// The solaris and linux implementations of park/unpark are fairly
// conservative for now, but can be improved. They currently use a
// mutex/condvar pair, plus _counter.
// Park decrements _counter if > 0, else does a condvar wait.  Unpark
// sets count to 1 and signals condvar.  Only one thread ever waits
// on the condvar. Contention seen when trying to park implies that someone
// is unparking you, so don't wait. And spurious returns are fine, so there
// is no need to track notifications.

#define MAX_SECS 100000000

// This code is common to linux and solaris and will be moved to a
// common place in dolphin.
//
// The passed in time value is either a relative time in nanoseconds
// or an absolute time in milliseconds. Either way it has to be unpacked
// into suitable seconds and nanoseconds components and stored in the
// given timespec structure.
// Given time is a 64-bit value and the time_t used in the timespec is only
// a signed-32-bit value (except on 64-bit Linux) we have to watch for
// overflow if times way in the future are given. Further on Solaris versions
// prior to 10 there is a restriction (see cond_timedwait) that the specified
// number of seconds, in abstime, is less than current_time  + 100,000,000.
// As it will be 28 years before "now + 100000000" will overflow we can
// ignore overflow and just impose a hard-limit on seconds using the value
// of "now + 100,000,000". This places a limit on the timeout of about 3.17
// years from "now".
//
static void unpackTime(timespec* absTime, bool isAbsolute, jlong time) {
  assert(time > 0, "convertTime");

  struct timeval now;
  int status = gettimeofday(&now, NULL);
  assert(status == 0, "gettimeofday");

  time_t max_secs = now.tv_sec + MAX_SECS;

  if (isAbsolute) {
    jlong secs = time / 1000;
    if (secs > max_secs) {
      absTime->tv_sec = max_secs;
    } else {
      absTime->tv_sec = secs;
    }
    absTime->tv_nsec = (time % 1000) * NANOSECS_PER_MILLISEC;
  } else {
    jlong secs = time / NANOSECS_PER_SEC;
    if (secs >= MAX_SECS) {
      absTime->tv_sec = max_secs;
      absTime->tv_nsec = 0;
    } else {
      absTime->tv_sec = now.tv_sec + secs;
      absTime->tv_nsec = (time % NANOSECS_PER_SEC) + now.tv_usec*1000;
      if (absTime->tv_nsec >= NANOSECS_PER_SEC) {
        absTime->tv_nsec -= NANOSECS_PER_SEC;
        ++absTime->tv_sec; // note: this must be <= max_secs
      }
    }
  }
  assert(absTime->tv_sec >= 0, "tv_sec < 0");
  assert(absTime->tv_sec <= max_secs, "tv_sec > max_secs");
  assert(absTime->tv_nsec >= 0, "tv_nsec < 0");
  assert(absTime->tv_nsec < NANOSECS_PER_SEC, "tv_nsec >= nanos_per_sec");
}
