// Here define some functions for better debuging process

#include "llvm/IR/Module.h"           // Module & Context
#include "llvm/IRReader/IRReader.h"   // parseIRFile
#include "llvm/Support/SourceMgr.h"   // SMDiagnostic
#include "llvm/Support/raw_ostream.h" // raw_string_ostream
#include <memory>                     // unique ptr
#include <string>                     // string


namespace llvm_utils {

static std::unique_ptr<llvm::Module> load_module(int argc, char *argv[], llvm::LLVMContext& ctx)
{
    if(argc < 2) exit(1);

    auto InputFile = std::string(argv[1]);
    llvm::SMDiagnostic Err;
    auto M = llvm::parseIRFile(InputFile, Err, ctx);
    if (!M) {
        exit(1);
    }

    return std::move(M);
}

static std::string to_string(llvm::Module* M) {
    std::string s;
    llvm::raw_string_ostream os(s);
    M->print(os, nullptr);
    os.flush();
    return s;
}

static std::string to_string(llvm::Instruction* inst) {
    std::string s;
    llvm::raw_string_ostream os(s);
    inst->print(os);
    os.flush();
    return s;
}

} // namespcae llvm_utils