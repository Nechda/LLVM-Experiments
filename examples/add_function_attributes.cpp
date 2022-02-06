#include <iostream>

// for loading module & dumping functions
#include "LUtils.hpp"

using namespace llvm;

int main(int argc, char *argv[])
{
    LLVMContext ctx;
    auto M = llvm_utils::load_module(argc, argv, ctx);

    // add `always inline` attribute to function f_0
    auto func_from_module = M->getFunction("f_0");
    func_from_module->addFnAttr(llvm::Attribute::AlwaysInline);
    
    std::cout << llvm_utils::to_string(M.get()) << std::endl;
    return 0;
}
