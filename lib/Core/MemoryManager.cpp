//===-- MemoryManager.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include "CoreStats.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "Context.h"

#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Solver.h"

#include "llvm/Support/CommandLine.h"
#include "malloc.h"

using namespace klee;

/***/
MemoryManager::MemoryManager(size_t pointerBitWidth) :
  pointerBitWidth(pointerBitWidth) {
  if (pointerBitWidth < 64) {
    // FIXME: KLEE should implement it's own memory manager for
    // the program we are executing and not invoke malloc so
    // we have finer control over the addresses we use.
    // HACK:
    // Try make sure malloc() gives us back
    // addresses that fit in a 32-bit integer when running
    // 32-bit code.
    // This works by preventing malloc() from using mmap()
    // so it uses sbrk() instead... gross
    mallopt(M_MMAP_MAX, 0);
  }
}

MemoryManager::~MemoryManager() { 
  while (!objects.empty()) {
    MemoryObject *mo = *objects.begin();
    if (!mo->isFixed)
      free((void *)mo->address);
    objects.erase(mo);
    delete mo;
  }
}

MemoryObject *MemoryManager::allocate(uint64_t size, bool isLocal, 
                                      bool isGlobal,
                                      const llvm::Value *allocSite) {
  if (size>10*1024*1024)
    klee_warning_once(0, "Large alloc: %u bytes.  KLEE may run out of memory.", (unsigned) size);
  
  uint64_t address = (uint64_t) (unsigned long) malloc((unsigned) size);
  if (!address)
    return 0;
  
  ++stats::allocations;
  MemoryObject *res = new MemoryObject(address, size, isLocal, isGlobal, false,
                                       allocSite, this);
  objects.insert(res);
  return res;
}

MemoryObject *MemoryManager::allocateFixed(uint64_t address, uint64_t size,
                                           const llvm::Value *allocSite) {
#ifndef NDEBUG
  for (objects_ty::iterator it = objects.begin(), ie = objects.end();
       it != ie; ++it) {
    MemoryObject *mo = *it;
    if (address+size > mo->address && address < mo->address+mo->size)
      klee_error("Trying to allocate an overlapping object");
  }
#endif

  ++stats::allocations;
  MemoryObject *res = new MemoryObject(address, size, false, true, true,
                                       allocSite, this);
  objects.insert(res);
  return res;
}

void MemoryManager::deallocate(const MemoryObject *mo) {
  assert(0);
}

void MemoryManager::markFreed(MemoryObject *mo) {
  if (objects.find(mo) != objects.end())
  {
    if (!mo->isFixed)
      free((void *)mo->address);
    objects.erase(mo);
  }
}
