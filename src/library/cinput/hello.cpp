#include "hello.h"
#include <iostream>

using namespace llvm;

using namespace tara;
using namespace tara::cinput;

char Hello::ID = 0;

bool
Hello::runOnFunction(llvm::Function &F) {
  std::cout << "HelloPass: ";
  std::cout << (std::string)(F.getName()) << '\n';

  for (bb &src : F){
    std::cout << (std::string)(src.getName()) << '\n';
    for(Instruction &i : src)
      std::cout << (std::string)(i.getOpcodeName()) << '\n';
    std::cout << "--------\n";
  }
  return false;
}


// char Hello::ID = 0;
// static RegisterPass<Hello> X("hello", "Hello World Pass",
//                              false /* Only looks at CFG */,
//                              false /* Analysis Pass */);
