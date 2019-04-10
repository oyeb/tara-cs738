#ifndef TARA_CINPUT_HELLO_PASS_H
#define TARA_CINPUT_HELLO_PASS_H

#include <set>
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

typedef llvm::BasicBlock bb;
typedef std::set<const bb*> bb_set_t;
typedef std::vector<const bb*> bb_vec_t;

namespace tara {
namespace cinput {
  class Hello : public llvm::FunctionPass {
  public:
    static char ID;
    Hello() : llvm::FunctionPass(ID) {}
    bool runOnFunction(llvm::Function &F) override;
  };
}
}

#endif
