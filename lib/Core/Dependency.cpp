
#include "Dependency.h"

using namespace klee;

namespace klee {

  unsigned long long VersionedAllocation::nextVersion = 0;

  VersionedAllocation::VersionedAllocation(llvm::Value *site) :
      site(site), version(nextVersion++) {}

  VersionedAllocation::~VersionedAllocation() {}

  bool VersionedAllocation::hasAllocationSite(llvm::Value *site) const {
    return this->site == site;
  }

  bool VersionedAllocation::isComposite() const {
    llvm::AllocaInst *inst = llvm::dyn_cast<llvm::AllocaInst>(site);

    assert(inst != 0);
    return llvm::isa<llvm::CompositeType>(inst->getAllocatedType());
  }

  void VersionedAllocation::print(llvm::raw_ostream& stream) const {
    stream << "A[";
    site->print(stream);
    stream << "#" << version;
    stream << "]";
  }

  /**/

  unsigned long long VersionedValue::nextVersion = 0;

  VersionedValue::VersionedValue(llvm::Value *value) :
      value(value), version(nextVersion++) {}

  VersionedValue::~VersionedValue() {}

  bool VersionedValue::hasValue(llvm::Value *value) const {
    return this->value == value;
  }

  void VersionedValue::print(llvm::raw_ostream& stream) const {
    stream << "V[";
    value->print(stream);
    stream << "#" << version;
    stream << "]";
  }

  /**/

  PointerEquality::PointerEquality(VersionedValue *value,
                                   VersionedAllocation *allocation) :
                                       value(value), allocation(allocation)
  {}

  PointerEquality::~PointerEquality() {}

  VersionedAllocation *PointerEquality::equals(VersionedValue *value) {
    return this->value == value ? allocation : 0;
  }

  void PointerEquality::print(llvm::raw_ostream& stream) const {
    stream << "(";
    value->print(stream);
    stream << "==";
    allocation->print(stream);
    stream << ")";
  }

  /**/

  StorageCell::StorageCell(VersionedAllocation *allocation,
                           VersionedValue* value) :
                               allocation(allocation),
                               value(value) {}

  StorageCell::~StorageCell() {}

  VersionedValue *StorageCell::stores(VersionedAllocation *allocation) const {
    return this->allocation == allocation ? this->value : 0;
  }

  VersionedAllocation *StorageCell::storageOf(VersionedValue *value) {
    return this->value == value ? this->allocation : 0;
  }

  void StorageCell::print(llvm::raw_ostream& stream) const {
    stream << "[";
    allocation->print(stream);
    stream << ",";
    value->print(stream);
    stream << "]";
  }

  /**/

  FlowsTo::FlowsTo(VersionedValue *source, VersionedValue *target)
      : source(source), target(target) {}

  FlowsTo::~FlowsTo() {}

  bool FlowsTo::depends(VersionedValue *source, VersionedValue *target) {
    return this->source == source && this->target == target;
  }

  void FlowsTo::print(llvm::raw_ostream &stream) const {
    source->print(stream);
    stream << "->";
    target->print(stream);
  }

  /**/

  DependencyFrame::DependencyFrame(llvm::Function *function)
      : function(function), argumentsList(0) {}

  DependencyFrame::~DependencyFrame() {
    // Delete the locally-constructed relations
    deletePointerVector(equalityList);
    deletePointerVector(storesList);
    deletePointerVector(flowsToList);

    // Delete the locally-constructed objects
    deletePointerVector(valuesList);
    deletePointerVector(allocationsList);
  }

  VersionedValue *DependencyFrame::getNewValue(llvm::Value *value) {
    VersionedValue *ret = new VersionedValue(value);
    valuesList.push_back(ret);
    return ret;
  }

  VersionedAllocation *DependencyFrame::getNewAllocation(llvm::Value *allocation) {
    VersionedAllocation *ret = new VersionedAllocation(allocation);
    allocationsList.push_back(ret);
    return ret;
  }

  VersionedValue *DependencyFrame::getLatestValue(llvm::Value *value) const {
    for (std::vector<VersionedValue *>::const_reverse_iterator
             it = valuesList.rbegin(),
             itEnd = valuesList.rend();
         it != itEnd; ++it) {
      if ((*it)->hasValue(value))
        return *it;
    }
    return 0;
  }

  VersionedAllocation *
  DependencyFrame::getLatestAllocation(llvm::Value *allocation) const {
    for (std::vector<VersionedAllocation *>::const_reverse_iterator
             it = allocationsList.rbegin(),
             itEnd = allocationsList.rend();
         it != itEnd; ++it) {
      if ((*it)->hasAllocationSite(allocation))
        return *it;
    }
    return 0;
  }

  VersionedAllocation *DependencyFrame::resolveAllocation(VersionedValue *val) {
    if (!val) return 0;
    for (std::vector< PointerEquality *>::reverse_iterator
	it = equalityList.rbegin(),
	itEnd = equalityList.rend(); it != itEnd; ++it) {
	VersionedAllocation *alloc = (*it)->equals(val);
	if (alloc)
	  return alloc;
    }
    return 0;
  }

  void DependencyFrame::addPointerEquality(VersionedValue *value,
                                           VersionedAllocation *allocation) {
    equalityList.push_back(new PointerEquality(value, allocation));
  }

  void DependencyFrame::updateStore(VersionedAllocation *allocation,
                                    VersionedValue *value) {
    storesList.push_back(new StorageCell(allocation, value));
  }

  void DependencyFrame::addDependency(VersionedValue *source,
                                      VersionedValue *target) {
    flowsToList.push_back(new FlowsTo(source, target));
  }

  std::vector<VersionedValue *>
  DependencyFrame::stores(VersionedAllocation *allocation) const {
    std::vector<VersionedValue *> ret;
    if (allocation->isComposite()) {
      for (std::vector<StorageCell *>::const_iterator it = storesList.begin(),
                                                      itEnd = storesList.end();
           it != itEnd; ++it) {
        VersionedValue *value = (*it)->stores(allocation);
        if (value) {
          ret.push_back(value);
        }
      }
    } else {
      for (std::vector<StorageCell *>::const_iterator it = storesList.begin(),
                                                      itEnd = storesList.end();
           it != itEnd; ++it) {
        VersionedValue *value = (*it)->stores(allocation);
        if (value) {
          ret.push_back(value);
          return ret;
        }
      }
    }
    return ret;
  }

  VersionedAllocation *DependencyFrame::storageOf(VersionedValue *value) {
    for (std::vector< StorageCell *>::iterator it = storesList.begin(),
	itEnd = storesList.end(); it != itEnd; ++it) {
	VersionedAllocation *ret = (*it)->storageOf(value);
	if (!ret)
	  return ret;
    }
    return 0;

  }

  bool DependencyFrame::depends(VersionedValue *source,
                                VersionedValue *target) {
    for (std::vector<FlowsTo *>::iterator it = flowsToList.begin(),
                                          itEnd = flowsToList.end();
         it != itEnd; ++it) {
      if ((*it)->depends(source, target))
        return true;
    }
    return false;
  }

  void DependencyFrame::bindArgument(const unsigned index,
                                     llvm::Value *value) {
    assert (index >= function->arg_size());
    argumentsList[index] = value;
  }

  std::vector<VersionedValue *>
  DependencyFrame::populateArgumentValuesList(llvm::CallInst *site) {
    unsigned numArgs = site->getCalledFunction()->arg_size();
    std::vector<VersionedValue *> argumentValuesList;
    for (unsigned i = numArgs; i > 0;) {
      llvm::Value *argOperand = site->getArgOperand(--i);
      VersionedValue *latestValue = getLatestValue(argOperand);
      argumentValuesList.push_back(latestValue ? latestValue
                                               : getNewValue(argOperand));
    }
    return argumentValuesList;
  }

  void DependencyFrame::bindCallArguments(
      std::vector<VersionedValue *> &argumentValuesList) {
    unsigned index = 0;
    for (llvm::Function::ArgumentListType::iterator
             it = function->getArgumentList().begin(),
             itEnd = function->getArgumentList().end();
         it != itEnd; ++it) {
      addDependency(argumentValuesList.back(), getNewValue(it));
      argumentValuesList.pop_back();
      ++index;
    }
  }

  void DependencyFrame::print(llvm::raw_ostream &stream,
                              const unsigned tab_num) const {
    std::string tabs = makeTabs(tab_num);
    stream << tabs << (function ? function->getName() : "BOTTOM") << " FRAME\n";
    stream << tabs << "EQUALITIES:";
    std::vector<PointerEquality *>::const_iterator equalityListBegin =
	equalityList.begin();
    std::vector<StorageCell *>::const_iterator storesListBegin =
	storesList.begin();
    std::vector<FlowsTo *>::const_iterator flowsToListBegin =
	flowsToList.begin();
    for (std::vector< PointerEquality *>::const_iterator it = equalityListBegin,
	itEnd = equalityList.end(); it != itEnd; ++it) {
	if (it != equalityListBegin)
	  stream << ",";
	(*it)->print(stream);
    }
    stream << "\n";
    stream << tabs << "STORAGE:";
    for (std::vector< StorageCell *>::const_iterator it = storesList.begin(),
	itEnd = storesList.end(); it != itEnd; ++it) {
	if (it != storesListBegin)
	  stream << ",";
	(*it)->print(stream);
    }
    stream << "\n";
    stream << tabs << "FLOWDEPENDENCY:";
    for (std::vector<FlowsTo *>::const_iterator it = flowsToList.begin(),
                                                itEnd = flowsToList.end();
         it != itEnd; ++it) {
      if (it != flowsToListBegin)
        stream << ",";
      (*it)->print(stream);
    }
  }

  void DependencyFrame::print(llvm::raw_ostream &stream) const {
    this->print(stream, 0);
  }

  /**/

  bool DependencyStack::buildLoadDependency(DependencyFrame *from,
                                            llvm::Value *fromValue,
                                            DependencyFrame *to,
                                            llvm::Value *toValue) {
    VersionedValue *arg = from->getLatestValue(fromValue);
    if (!arg)
      return false;

    VersionedAllocation *alloc = from->resolveAllocation(arg);
    if (alloc) {
      std::vector<VersionedValue *> valList = from->stores(alloc);
      if (valList.size() > 0) {
        for (std::vector<VersionedValue *>::iterator it = valList.begin(),
                                                     itEnd = valList.end();
             it != itEnd; ++it) {
          VersionedAllocation *alloc2 = from->resolveAllocation(*it);
          if (alloc2) {
            to->addPointerEquality(to->getNewValue(toValue), alloc2);
          } else {
            to->addDependency(*it, to->getNewValue(toValue));
          }
        }
      } else {
        // We could not find the stored value, create
        // a new one.
        to->updateStore(alloc, to->getNewValue(toValue));
      }
    }

    return true;
  }

  bool DependencyStack::isLocal(
      const std::vector<DependencyFrame *> &currentLocalFrames,
      DependencyFrame *frame) {
    return std::find(currentLocalFrames.begin(), currentLocalFrames.end(),
                     frame) != currentLocalFrames.end();
  }

  DependencyStack::DependencyStack(llvm::Function *function,
                                   DependencyStack *prev)
      : top(new DependencyFrame(function)), tail(prev) {
    // Whenever a stack was defined, it should have a global frame.
    assert (!prev || prev->global);

    // Inherit the global frame from the tail or create a new one.
    global = (prev ? prev->global : new DependencyFrame(0));

    // Propagate argument values from the previous stack
    if (prev) {
      argumentValuesList = prev->argumentValuesList;
    }
  }

  DependencyStack::~DependencyStack() {
    delete top;
  }

  DependencyFrame *DependencyStack::car() const {
    return top;
  }

  DependencyStack* DependencyStack::cdr() const {
    return tail;
  }

  void DependencyStack::execute(
      const std::vector<DependencyFrame *> &currentLocalFrames,
      llvm::Instruction *i) {
    switch(i->getOpcode()) {
      case llvm::Instruction::Alloca: {
	top->addPointerEquality(top->getNewValue(i),
	                        top->getNewAllocation(i));
	break;
      }
      case llvm::Instruction::Load: {
        if (!buildLoadDependency(top, i->getOperand(0), top, i) &&
            llvm::isa<llvm::GlobalValue>(i->getOperand(0))) {
          if (!buildLoadDependency(global, i->getOperand(0), top, i)) {
            VersionedValue *arg = global->getNewValue(i->getOperand(0));
            VersionedAllocation *alloc =
                global->getNewAllocation(i->getOperand(0));
            global->addPointerEquality(arg, alloc);
            top->updateStore(alloc, top->getNewValue(i));
          }
        }
        break;
      }
      case llvm::Instruction::Store: {
	VersionedValue *dataArg = top->getLatestValue(i->getOperand(0));
	VersionedAllocation *address =
	    top->resolveAllocation(top->getLatestValue(i->getOperand(1)));

	if (dataArg) {
	    // There is dependency to store (not a constant etc.)
	    if (address) {
		top->updateStore(address, dataArg);
	    }
	} else {
	    // There is no dependency found, we should create
	    // a new value
	    if (address) {
		top->updateStore(address,
		                 top->getNewValue(i->getOperand(0)));
	    }
	}
	break;
      }
      case llvm::Instruction::GetElementPtr: {
        VersionedValue *arg = top->getLatestValue(i->getOperand(0));
        assert(arg != 0);

        VersionedAllocation *alloc = top->resolveAllocation(arg);
        if (alloc) {
          // We simply propagate the pointer to the current
          // value field-insensitively.
          top->addPointerEquality(top->getNewValue(i), alloc);
        }
        break;
      }
      case llvm::Instruction::Trunc:
      case llvm::Instruction::ZExt:
      case llvm::Instruction::SExt:
      case llvm::Instruction::IntToPtr:
      case llvm::Instruction::PtrToInt:
      case llvm::Instruction::BitCast:
      case llvm::Instruction::FPTrunc:
      case llvm::Instruction::FPExt:
      case llvm::Instruction::FPToUI:
      case llvm::Instruction::FPToSI:
      case llvm::Instruction::UIToFP:
      case llvm::Instruction::SIToFP:
      case llvm::Instruction::ExtractValue:
	{
	  VersionedValue *val = top->getLatestValue(i->getOperand(0));
	  if (val) {
	      VersionedAllocation *alloc = top->resolveAllocation(val);
	      if (alloc) {
		  top->addPointerEquality(top->getNewValue(i), alloc);
	      } else {
		  top->addDependency(val, top->getNewValue(i));
	      }
	  }
	  break;
	}
      case llvm::Instruction::Select:
	{
	  VersionedValue *lhs = top->getLatestValue(i->getOperand(1));
	  VersionedValue *rhs = top->getLatestValue(i->getOperand(2));
	  VersionedValue *newValue = 0;
	  if (lhs) {
	      newValue = top->getNewValue(i);
	      top->addDependency(lhs, newValue);
	  }
	  if (rhs) {
	      if (newValue)
		top->addDependency(rhs, newValue);
	      else
		top->addDependency(rhs, top->getNewValue(i));
	  }
	  break;
	}
      case llvm::Instruction::Add:
      case llvm::Instruction::Sub:
      case llvm::Instruction::Mul:
      case llvm::Instruction::UDiv:
      case llvm::Instruction::SDiv:
      case llvm::Instruction::URem:
      case llvm::Instruction::SRem:
      case llvm::Instruction::And:
      case llvm::Instruction::Or:
      case llvm::Instruction::Xor:
      case llvm::Instruction::Shl:
      case llvm::Instruction::LShr:
      case llvm::Instruction::AShr:
      case llvm::Instruction::ICmp:
      case llvm::Instruction::FAdd:
      case llvm::Instruction::FSub:
      case llvm::Instruction::FMul:
      case llvm::Instruction::FDiv:
      case llvm::Instruction::FRem:
      case llvm::Instruction::FCmp:
      case llvm::Instruction::InsertValue:
	{
	  VersionedValue *lhs = top->getLatestValue(i->getOperand(0));
	  VersionedValue *rhs = top->getLatestValue(i->getOperand(1));
	  VersionedValue *newValue = 0;
	  if (lhs) {
	      newValue = top->getNewValue(i);
	      top->addDependency(lhs, newValue);
	  }
	  if (rhs) {
	      if (newValue)
		top->addDependency(rhs, newValue);
	      else
		top->addDependency(rhs, top->getNewValue(i));
	  }
	  break;
	}
      case llvm::Instruction::Invoke:
      case llvm::Instruction::Call:
	{
	  break;
	}
      case llvm::Instruction::Ret:
	{
	  break;
	}
      default:
	break;
    }

  }

  void DependencyStack::registerCallArguments(llvm::Instruction *instr) {
    llvm::CallInst *site = llvm::dyn_cast<llvm::CallInst>(instr);

    if (!site)
      return;

    argumentValuesList = top->populateArgumentValuesList(site);
  }

  void DependencyStack::bindCallArguments() {
    top->bindCallArguments(argumentValuesList);
  }

  void DependencyStack::print(llvm::raw_ostream& stream) const {
    this->print(stream, 0);
  }

  void DependencyStack::print(llvm::raw_ostream& stream,
                              const unsigned int tab_num) const {
    std::string tabs = makeTabs(tab_num);
    printGlobalFrame(stream, tab_num);
    stream << "\n";
    stream << tabs << "----------------------------------------\n";
    printStack(stream, tab_num);
  }

  void DependencyStack::printGlobalFrame(llvm::raw_ostream& stream,
                                         const unsigned int tab_num) const {
    std::string tabs = makeTabs(tab_num);
    stream << tabs << "------------- Global Frame -------------\n";
    global->print(stream, tab_num);
  }

  void DependencyStack::printStack(llvm::raw_ostream& stream,
                                   const unsigned int tab_num) const {
    top->print(stream, tab_num);
    if (tail) {
	stream << "\n";
        stream << makeTabs(tab_num)
               << "----------------------------------------\n";
        tail->printStack(stream, tab_num);
    }
  }

  /**/

  template<typename T>
  void deletePointerVector(std::vector<T*>& list) {
    typedef typename std::vector<T*>::iterator IteratorType;

    for (IteratorType it = list.begin(),
	itEnd = list.end(); it != itEnd; ++it) {
	delete *it;
    }
    list.clear();
  }

  std::string makeTabs(const unsigned int tab_num) {
    std::string tabs_string;
    for (unsigned int i = 0; i < tab_num; i++) {
      tabs_string += appendTab(tabs_string);
    }
    return tabs_string;
  }

  std::string appendTab(const std::string &prefix) {
    return prefix + "        ";
  }
}
