#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Verifier.h"
#include "llvm/LTO/LTOBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Coroutines.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/BoundsChecking.h"
#include "llvm/Transforms/Instrumentation/GCOVProfiler.h"
#include "llvm/Transforms/Instrumentation/HWAddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/InstrProfiling.h"
#include "llvm/Transforms/Instrumentation/MemorySanitizer.h"
#include "llvm/Transforms/Instrumentation/SanitizerCoverage.h"
#include "llvm/Transforms/Instrumentation/ThreadSanitizer.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/CanonicalizeAliases.h"
#include "llvm/Transforms/Utils/EntryExitInstrumenter.h"
#include "llvm/Transforms/Utils/NameAnonGlobals.h"
#include "llvm/Transforms/Utils/SymbolRewriter.h"
#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/SpeculateAnalyses.h"
#include "llvm/ExecutionEngine/Orc/Speculation.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"

#include "llvm/Transforms/Utils/SplitModule.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/IR/PassTimingInfo.h"



#include <memory>
#include <iostream>
#include <fstream>
#include <chrono>

/*
void clang::EmbedBitcode(TheModule.get(), CodeGenOpts, MainFile->getMemBufferRef());



void clang::EmitBackendOutput(Diagnostics, CI.getHeaderSearchOpts(), CodeGenOpts,
                      TargetOpts, CI.getLangOpts(),
                      CI.getTarget().getDataLayout(), TheModule.get(), BA,
                      std::move(OS));

*/

using namespace llvm;


struct ScopeTimer {
    using time_t_ = std::chrono::steady_clock::time_point;
    time_t_ begin_;
    time_t_ end_;
    ScopeTimer()
    {
        begin_ = std::chrono::steady_clock::now();
    }
    ~ScopeTimer()
    {
        end_ = std::chrono::steady_clock::now();
        size_t diff_int = std::chrono::duration_cast<std::chrono::microseconds>(end_ - begin_).count();
        double diff = (double) diff_int / 1e+6;
        std::cout << "Time diff = " << diff << std::endl;
    }
};


void AddPassesForELF(TargetMachine& TM, legacy::PassManager &CodeGenPasses,
            raw_pwrite_stream &OS,
            CodeGenOpt::Level OptimizationLevel) {


  CodeGenFileType CGFT = CGFT_ObjectFile;

  //if (OptimizationLevel != CodeGenOpt::None)
  //  CodeGenPasses.add(createObjCARCContractPass());

  bool has_error = TM.addPassesToEmitFile(CodeGenPasses, OS, nullptr, CGFT);
  if(has_error) {
      std::cerr << "can`t add passes fot generating object file" << std::endl;
  }
}

void compile_module(Module &M, TargetMachine &TM) {
    /* Function optimization */
    {
        auto FPM = std::make_unique<llvm::legacy::FunctionPassManager>(&M);
        auto PM = std::make_unique<llvm::legacy::PassManager>();

        llvm::PassManagerBuilder builder;
        builder.OptLevel = 0;
        builder.populateFunctionPassManager(*FPM);
        builder.populateModulePassManager(*PM);

        FPM->doInitialization();
        for(auto& func : M) {
            FPM->run(func);
        }
        FPM->doFinalization();

        PM->run(M);

        #ifndef NDEBUG
            M.dump();
        #endif
    }


    /* Generate asm code */
    SmallVector<char, 0> ObjBufferSV;
    raw_svector_ostream ObjStream(ObjBufferSV);

    auto PM = std::make_unique<llvm::legacy::PassManager>();
    AddPassesForELF(TM, *PM, ObjStream, CodeGenOpt::None);
    PM->run(M);

    /* And write it into file */
    auto ObjBuffer = std::make_unique<SmallVectorMemoryBuffer>(std::move(ObjBufferSV));
    auto Obj = object::ObjectFile::createObjectFile(ObjBuffer->getMemBufferRef(), llvm::file_magic::elf_relocatable);
    if (!Obj)
        exit(1);

    const auto& buf_wrapper = ObjBuffer->getMemBufferRef();
    
    auto raw_data = buf_wrapper.getBufferStart();
    auto raw_size = buf_wrapper.getBufferSize();
    
    std::ofstream fs("out.o", std::ios::out | std::ios::binary);
    fs.write(raw_data, raw_size);
    fs.close();
}

std::unique_ptr<Module> load_module(int argc, char *argv[], LLVMContext& ctx)
{
  if(argc != 2) exit(1);

  auto InputFile = std::string(argv[1]);
  SMDiagnostic Err;
  auto M = parseIRFile(InputFile, Err, ctx);
  if (!M) {
    Err.print(argv[0], errs());
    exit(1);
  }

  return std::move(M);
}

std::unique_ptr<TargetMachine> get_tt() {
    // Create the TargetMachine for generating code.
    std::string Error;
    std::string Triple = sys::getDefaultTargetTriple();
    const llvm::Target *TheTarget = TargetRegistry::lookupTarget(Triple, Error);

    Optional<llvm::CodeModel::Model> CM = llvm::CodeModel::Small;
    std::string FeaturesStr = ""; //llvm::join(TargetOpts.Features.begin(), TargetOpts.Features.end(), ",");
    llvm::Reloc::Model RM = Reloc::Model::PIC_;
    CodeGenOpt::Level OptLevel = CodeGenOpt::None;

    llvm::TargetOptions Options;

    std::unique_ptr<TargetMachine> TM;
    TM.reset(TheTarget->createTargetMachine(Triple, sys::getHostCPUName(), FeaturesStr,
                                            Options, RM, CM, OptLevel, 1));

    return std::move(TM);
}

int main(int argc, char** argv) {
    InitLLVM X(argc, argv);
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    auto ctx = std::make_unique<LLVMContext>();
    auto M = load_module(argc, argv, *ctx);

    #if 0
    auto JTMB = std::move(cantFail(orc::JITTargetMachineBuilder::detectHost()));
    JTMB.setRelocationModel(llvm::Reloc::Model::PIC_);
    JTMB.setCodeGenOptLevel(CodeGenOpt::None);
    auto TM = cantFail(JTMB.createTargetMachine());
    #else
    auto TM = std::move(get_tt());
    #endif

    TimePassesIsEnabled = true;

    {
    ScopeTimer t___;
    
    compile_module(*M, *TM);
    }
    

    return 0;
}