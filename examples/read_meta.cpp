#include <iostream>
#include <llvm/IR/Constants.h>

// for loading module & dumping functions
#include "LUtils.hpp"

using namespace llvm;

int main(int argc, char *argv[])
{
    LLVMContext ctx;
    auto M = llvm_utils::load_module(argc, argv, ctx);

    auto node = M->getOrInsertNamedMetadata("variable_name");
    auto md_string = MDString::get(ctx, "some string");
    node->addOperand(MDNode::get(ctx, md_string));

    auto md_const = ConstantAsMetadata::get(ConstantInt::get(IntegerType::getInt32Ty(ctx), 0x1000));
    node->addOperand(MDNode::get(ctx, md_const));
    
    std::cout << llvm_utils::to_string(M.get()) << std::endl;

    auto val = dyn_cast<ConstantAsMetadata>(node->getOperand(1)->getOperand(0))->getValue();
    auto casted_val = cast<ConstantInt>(val);
    auto actual_int = casted_val->getSExtValue();
    std::cout << "Actual int from module = " << actual_int << std::endl;

    return 0;
}
