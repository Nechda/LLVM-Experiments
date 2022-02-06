#include <iostream>
#include <llvm/Passes/PassBuilder.h>

// for loading module & dumping functions
#include "LUtils.hpp"

using namespace llvm;

void opt_module(Module* M)
{
    llvm::PassBuilder pass_builder;
    llvm::LoopAnalysisManager loop_an_mg;
    llvm::FunctionAnalysisManager func_an_mg;
    llvm::CGSCCAnalysisManager cgscc_an_mg ;
    llvm::ModuleAnalysisManager module_an_mg;

    pass_builder.registerModuleAnalyses(module_an_mg);
    pass_builder.registerCGSCCAnalyses(cgscc_an_mg);
    pass_builder.registerFunctionAnalyses(func_an_mg);
    pass_builder.registerLoopAnalyses(loop_an_mg);

    pass_builder.crossRegisterProxies(loop_an_mg, func_an_mg, cgscc_an_mg, module_an_mg);

    auto MP = pass_builder.buildPerModuleDefaultPipeline(PassBuilder::OptimizationLevel::O2);

    MP.run(*M, module_an_mg);
}

int main(int argc, char *argv[])
{
    LLVMContext ctx;
    auto M = llvm_utils::load_module(argc, argv, ctx);

    opt_module(M.get());

    std::cout << llvm_utils::to_string(M.get()) << std::endl;
    return 0;
}
