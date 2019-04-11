#include "hello.h"
#include "llvm/IR/DebugLoc.h"
#include <iostream>

#define DEBUG

#ifdef DEBUG
#define D(x) do { std::cerr << x; } while (0)
#else
#define D(x)
#endif

using namespace tara;
using namespace tara::cinput;

char Hello::ID = 0;

bool
Hello::runOnFunction(llvm::Function &F) {
  std::cout << "HelloPass: ";
  std::cout << (std::string)(F.getName()) << '\n';
  uint heap_alloc_count = 0;
  
  for (bb &src : F){
    D((std::string)(src.getName()) << '\n');
    for(llvm::Instruction &instr : src){
      D((std::string)(instr.getOpcodeName()));
      if( auto call = llvm::dyn_cast<llvm::CallInst>(&instr) ) {
	llvm::Function* fp = call->getCalledFunction();
	D(' ' << (std::string)(fp->getName()));
	if (fp->getName() == "malloc" || fp->getName() == "calloc"){
	  heap_alloc_count++;
	  D("  type: " << call->getType()->getTypeID() << " name: " << call->getName().str() << " hasName: " << call->hasName());
	}
      } else if( auto alloc = llvm::dyn_cast<llvm::AllocaInst>(&instr) ) {
	auto type = alloc->getAllocatedType();
	D("type id: " << type->getTypeID());
      }
      D('\n');
    }
  }
  std::cout << "\nHelloPass: Found " << heap_alloc_count << " heap alloc site(s)\n";
  return false;
}


// char Hello::ID = 0;
// static RegisterPass<Hello> X("hello", "Hello World Pass",
//                              false /* Only looks at CFG */,
//                              false /* Analysis Pass */);
