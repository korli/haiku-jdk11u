/*
 * Copyright (c) 2012, 2018, Oracle and/or its affiliates. All rights reserved.
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
#include "precompiled.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/os.hpp"
#include "runtime/os_perf.hpp"
#include "vm_version_ext_x86.hpp"


static const double NANOS_PER_SEC = 1000000000.0;

class CPUPerformanceInterface::CPUPerformance : public CHeapObj<mtInternal> {
   friend class CPUPerformanceInterface;
 private:
  long _total_cpu_nanos;
  long _total_csr_nanos;
  long _jvm_user_nanos;
  long _jvm_system_nanos;
  long _jvm_context_switches;
  long _used_ticks;
  long _total_ticks;
  int  _active_processor_count;

  bool now_in_nanos(long* resultp) {
    timeval current_time;
    if (gettimeofday(&current_time, NULL) != 0) {
      // Error getting current time
      return false;
    }
    *resultp = current_time.tv_sec * NANOS_PER_SEC + 1000L * current_time.tv_usec;
    return true;
  }

  double normalize(double value) {
    return MIN2<double>(MAX2<double>(value, 0.0), 1.0);
  }
  int cpu_load(int which_logical_cpu, double* cpu_load);
  int context_switch_rate(double* rate);
  int cpu_load_total_process(double* cpu_load);
  int cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad);

  CPUPerformance(const CPUPerformance& rhs); // no impl
  CPUPerformance& operator=(const CPUPerformance& rhs); // no impl
 public:
  CPUPerformance();
  bool initialize();
  ~CPUPerformance();
};

CPUPerformanceInterface::CPUPerformance::CPUPerformance() {
  _total_cpu_nanos= 0;
  _total_csr_nanos= 0;
  _jvm_context_switches = 0;
  _jvm_user_nanos = 0;
  _jvm_system_nanos = 0;
  _used_ticks = 0;
  _total_ticks = 0;
  _active_processor_count = 0;
}

bool CPUPerformanceInterface::CPUPerformance::initialize() {
  return true;
}

CPUPerformanceInterface::CPUPerformance::~CPUPerformance() {
}

int CPUPerformanceInterface::CPUPerformance::cpu_load(int which_logical_cpu, double* cpu_load) {
  return FUNCTIONALITY_NOT_IMPLEMENTED;
}

int CPUPerformanceInterface::CPUPerformance::cpu_load_total_process(double* cpu_load) {
  return FUNCTIONALITY_NOT_IMPLEMENTED;
}

int CPUPerformanceInterface::CPUPerformance::cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad) {
  return FUNCTIONALITY_NOT_IMPLEMENTED;
}

int CPUPerformanceInterface::CPUPerformance::context_switch_rate(double* rate) {
  return FUNCTIONALITY_NOT_IMPLEMENTED;
}

CPUPerformanceInterface::CPUPerformanceInterface() {
  _impl = NULL;
}

bool CPUPerformanceInterface::initialize() {
  _impl = new CPUPerformanceInterface::CPUPerformance();
  return _impl != NULL && _impl->initialize();
}

CPUPerformanceInterface::~CPUPerformanceInterface() {
  if (_impl != NULL) {
    delete _impl;
  }
}

int CPUPerformanceInterface::cpu_load(int which_logical_cpu, double* cpu_load) const {
  return _impl->cpu_load(which_logical_cpu, cpu_load);
}

int CPUPerformanceInterface::cpu_load_total_process(double* cpu_load) const {
  return _impl->cpu_load_total_process(cpu_load);
}

int CPUPerformanceInterface::cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad) const {
  return _impl->cpu_loads_process(pjvmUserLoad, pjvmKernelLoad, psystemTotalLoad);
}

int CPUPerformanceInterface::context_switch_rate(double* rate) const {
  return _impl->context_switch_rate(rate);
}

class SystemProcessInterface::SystemProcesses : public CHeapObj<mtInternal> {
  friend class SystemProcessInterface;
 private:
  SystemProcesses();
  bool initialize();
  SystemProcesses(const SystemProcesses& rhs); // no impl
  SystemProcesses& operator=(const SystemProcesses& rhs); // no impl
  ~SystemProcesses();

  //information about system processes
  int system_processes(SystemProcess** system_processes, int* no_of_sys_processes) const;
};

SystemProcessInterface::SystemProcesses::SystemProcesses() {
}

bool SystemProcessInterface::SystemProcesses::initialize() {
  return true;
}

SystemProcessInterface::SystemProcesses::~SystemProcesses() {
}
int SystemProcessInterface::SystemProcesses::system_processes(SystemProcess** system_processes, int* no_of_sys_processes) const {
  return FUNCTIONALITY_NOT_IMPLEMENTED;
}

int SystemProcessInterface::system_processes(SystemProcess** system_procs, int* no_of_sys_processes) const {
  return _impl->system_processes(system_procs, no_of_sys_processes);
}

SystemProcessInterface::SystemProcessInterface() {
  _impl = NULL;
}

bool SystemProcessInterface::initialize() {
  _impl = new SystemProcessInterface::SystemProcesses();
  return _impl != NULL && _impl->initialize();
}

SystemProcessInterface::~SystemProcessInterface() {
  if (_impl != NULL) {
    delete _impl;
 }
}

CPUInformationInterface::CPUInformationInterface() {
  _cpu_info = NULL;
}

bool CPUInformationInterface::initialize() {
  _cpu_info = new CPUInformation();

  if (NULL == _cpu_info) {
    return false;
  }
  _cpu_info->set_number_of_hardware_threads(VM_Version_Ext::number_of_threads());
  _cpu_info->set_number_of_cores(VM_Version_Ext::number_of_cores());
  _cpu_info->set_number_of_sockets(VM_Version_Ext::number_of_sockets());
  _cpu_info->set_cpu_name(VM_Version_Ext::cpu_name());
  _cpu_info->set_cpu_description(VM_Version_Ext::cpu_description());

  return true;
}

CPUInformationInterface::~CPUInformationInterface() {
  if (_cpu_info != NULL) {
    if (_cpu_info->cpu_name() != NULL) {
      const char* cpu_name = _cpu_info->cpu_name();
      FREE_C_HEAP_ARRAY(char, cpu_name);
      _cpu_info->set_cpu_name(NULL);
    }
    if (_cpu_info->cpu_description() != NULL) {
      const char* cpu_desc = _cpu_info->cpu_description();
      FREE_C_HEAP_ARRAY(char, cpu_desc);
      _cpu_info->set_cpu_description(NULL);
    }
    delete _cpu_info;
  }
}

int CPUInformationInterface::cpu_information(CPUInformation& cpu_info) {
  if (NULL == _cpu_info) {
    return OS_ERR;
  }

  cpu_info = *_cpu_info; // shallow copy assignment
  return OS_OK;
}

class NetworkPerformanceInterface::NetworkPerformance : public CHeapObj<mtInternal> {
  friend class NetworkPerformanceInterface;
 private:
  NetworkPerformance();
  NetworkPerformance(const NetworkPerformance& rhs); // no impl
  NetworkPerformance& operator=(const NetworkPerformance& rhs); // no impl
  bool initialize();
  ~NetworkPerformance();
  int network_utilization(NetworkInterface** network_interfaces) const;
};

NetworkPerformanceInterface::NetworkPerformance::NetworkPerformance() {
}

bool NetworkPerformanceInterface::NetworkPerformance::initialize() {
  return true;
}

NetworkPerformanceInterface::NetworkPerformance::~NetworkPerformance() {
}

int NetworkPerformanceInterface::NetworkPerformance::network_utilization(NetworkInterface** network_interfaces) const {
  return FUNCTIONALITY_NOT_IMPLEMENTED;
}

NetworkPerformanceInterface::NetworkPerformanceInterface() {
  _impl = NULL;
}

NetworkPerformanceInterface::~NetworkPerformanceInterface() {
  if (_impl != NULL) {
    delete _impl;
  }
}

bool NetworkPerformanceInterface::initialize() {
  _impl = new NetworkPerformanceInterface::NetworkPerformance();
  return _impl != NULL && _impl->initialize();
}

int NetworkPerformanceInterface::network_utilization(NetworkInterface** network_interfaces) const {
  return _impl->network_utilization(network_interfaces);
}
