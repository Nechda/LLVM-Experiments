#include <iostream>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/TargetSelect.h>

#include <string>

using namespace llvm;
using namespace std;

typedef unsigned char   ui8;
typedef unsigned short  ui16;
typedef unsigned int    ui32;
typedef unsigned long   ui64;

typedef char    i8;
typedef short   i16;
typedef int     i32;
typedef long    i64;

const ui32 REG_FILE_SIZE = 4;
ui32 CPU_REG_FILE[REG_FILE_SIZE] = { 4,7,1,2 };


void printTestedArray()
{
    for (ui8 i = 0; i < REG_FILE_SIZE; i++)
        printf("reg[%d]:%d \t", i, CPU_REG_FILE[i]);
    printf("\n");
}

int main()
{
    LLVMContext context;
    IRBuilder<> builder(context);
    Module* module = new Module("Main_module", context);

    /*creating an array*/
    ArrayType* regFileType  = ArrayType::get(builder.getInt32Ty(), REG_FILE_SIZE);
                              module->getOrInsertGlobal("regFile", regFileType);
    GlobalVariable* regFile = module->getNamedGlobal("regFile");

    /*define i64 main_func()*/
    FunctionType* funcType = FunctionType::get(builder.getInt64Ty(), false);
    Function*     mainFunc = Function::Create(funcType, Function::ExternalLinkage, "main_func", module);
    BasicBlock*   entryBB  = BasicBlock::Create(context, "entry", mainFunc);
    builder.SetInsertPoint(entryBB);

    /* %addr = i32*  getelementptr inbounds ([4 x i32], [4 x i32]* @regFile, i32 0, i32 2) */
    Value* ptr_to_data = builder.CreateConstGEP2_32(
        regFileType,
        regFile,
        0,
        2,
        "addr"
    );

    /* store i32 0xAABBCCDD, i32* %addr */
    builder.CreateStore(builder.getInt32(0xAABBCCDD), ptr_to_data);
    /* ret i64 ptrtoint (%addr to i64)*/
    builder.CreateRet(builder.CreatePtrToInt(ptr_to_data, builder.getInt64Ty()));

    string s;
    raw_string_ostream os(s);
    module->print(os, nullptr);
    os.flush();
    printf("#[LLVM IR]:\n");
    printf("%s\n", s.c_str());
    printf("#[LLVM IR] END\n\n\n");
    system("pause");


    InitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMLinkInMCJIT();

    string err_str;
    ExecutionEngine* ee = EngineBuilder(unique_ptr<Module>(module))
        .setErrorStr(&err_str)
        .create();
    if (!ee)
    {
        printf("Could not create ExecutionEngine: %s", err_str.c_str());
        system("pause");
        return 1;
    }
    ee->addGlobalMapping(regFile, CPU_REG_FILE);
    ee->finalizeObject();


    printTestedArray();

    std::cout << "#[LLVM IR EXEC]:\n";
    GenericValue result = ee->runFunction(mainFunc, vector<GenericValue>());
    std::cout << "#[LLVM IR EXEC] END\n";

    ui64 r = *result.IntVal.getRawData();
    printf("     Returned: 0x%lX\n", r);
    printf("&Registers[0]: 0x%lX\n", &CPU_REG_FILE[0]);

    printTestedArray();

    system("pause");
    return 0;
}