#pragma once

#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
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
#include "llvm/Support/TargetRegistry.h"

#include "llvm/Transforms/Utils/SplitModule.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>

#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <iostream>
#include <fstream>
#include <chrono>


namespace llvm {

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

    ;
    std::unique_ptr<TargetMachine> TM;
    TM.reset(TheTarget->createTargetMachine(Triple, sys::getHostCPUName(), FeaturesStr,
                                            Options, RM, CM, OptLevel, 1));

    return std::move(TM);
}
}

namespace llvm { namespace orc {
    class Compiler {
      public:
        Compiler(TargetMachine &TM) : compilator(TM) {};
        
        Expected<std::unique_ptr<MemoryBuffer>> compile(Module& M)
        {
            optimize(M);
            return compilator(M);
        }
      private:
        SimpleCompiler compilator;

        void optimize(Module& M)
        {
            auto FPM = std::make_unique<llvm::legacy::FunctionPassManager>(&M);
            auto PM = std::make_unique<llvm::legacy::PassManager>();

            llvm::PassManagerBuilder builder;
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
    };
}}


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

    void start() {
        begin_ = std::chrono::steady_clock::now();
    }

    void stop() {
        end_ = std::chrono::steady_clock::now();
    }
    
    void append() {
        total_time += std::chrono::duration_cast<std::chrono::microseconds>(end_ - begin_).count();
    }

    size_t total_time = 0;
};

namespace llvm { namespace orc {
    class JIT {
      public:
        static Expected<std::unique_ptr<JIT>> Create() {

            auto JTMB = orc::JITTargetMachineBuilder::detectHost();
            if (!JTMB)
                return JTMB.takeError();
            JTMB->setCodeGenOptLevel(CodeGenOpt::None);

            /*
            TargetOptions options;
            options.EnableFastISel = 1;
            options.EnableGlobalISel = 1;
            JTMB->setOptions(options);
            */

            auto DL = JTMB->getDefaultDataLayoutForTarget();
            if (!DL)
              return DL.takeError();

            auto ES = std::make_unique<ExecutionSession>();

            auto ProcessSymbolsSearchGenerator =
                DynamicLibrarySearchGenerator::GetForCurrentProcess(DL->getGlobalPrefix());
            if (!ProcessSymbolsSearchGenerator)
                return ProcessSymbolsSearchGenerator.takeError();


            std::unique_ptr<JIT>
                jit(
                     new JIT(
                         std::move(ES), std::move(*DL), std::move(get_tt()),
                         std::move(*ProcessSymbolsSearchGenerator)
                     )
                );
            return std::move(jit);
        }
        

        ExecutionSession &getES() {return *ES; };
        Error addModule(ThreadSafeModule TSM) { return TransformLayer.add(MainJD, std::move(TSM)); }
        Expected<JITEvaluatedSymbol> lookup(StringRef name) { return ES->lookup({&MainJD}, Mangle(name)); }
      private:
        /* Constructor */
        JIT(
            std::unique_ptr<ExecutionSession> ES,
            DataLayout DL,
            std::unique_ptr<TargetMachine> TM,
            std::unique_ptr<DynamicLibrarySearchGenerator> ProcessSymbolsGenerator
        ) :
            ES(std::move(ES)),
            DL(std::move(DL)),
            TM(std::move(TM)),
            Mangle(*this->ES, this->DL),
            MainJD(this->ES->createJITDylib("<main>")),
            ObjLayer(*this->ES, createMM),
            ObjTransformLayer(*this->ES, ObjLayer, objectTransformation),
            CompileLayer(*this->ES, ObjTransformLayer, std::make_unique<SimpleCompiler>(*this->TM)),
            TransformLayer(*this->ES, CompileLayer, optimizeModule)
        {
            MainJD.addGenerator(std::move(ProcessSymbolsGenerator));
        }

        /* Optimizations */
        static Expected<ThreadSafeModule>
        optimizeModule(ThreadSafeModule TSM, const MaterializationResponsibility &R) {
            auto& M = *TSM.getModuleUnlocked();
            
            auto FPM = std::make_unique<llvm::legacy::FunctionPassManager>(&M);
            auto PM = std::make_unique<llvm::legacy::PassManager>();

            llvm::PassManagerBuilder builder;
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
          return TSM;
        }

      private:

        static std::unique_ptr<SectionMemoryManager> createMM() { return std::make_unique<SectionMemoryManager>(); }

        static std::unique_ptr<MemoryBuffer> objectTransformation(std::unique_ptr<MemoryBuffer> input_buffer)
        {
            std::ofstream fs("out.o", std::ios::out | std::ios::binary);
            fs.write(input_buffer->getBufferStart(), input_buffer->getBufferSize());
            fs.close();
            //time_stamp.print();
            //std::cout << "End of compilation get generated code; size = " << input_buffer->getBuffer().size() << std::endl;
         
            /*
            auto ObjBuffer = MemoryBuffer::getMemBuffer(input_buffer->getMemBufferRef(), false);
 
            auto Obj = cantFail(object::ObjectFile::createObjectFile(*ObjBuffer));
 
             for (auto &Sym : Obj->symbols())
            {
                auto SymName = cantFail(Sym.getName());
                std::cout << SymName.str() << std::endl;
            }
            */
         
            return std::move(input_buffer);
        }


      private:

        /* Interanl structures */
        std::unique_ptr<ExecutionSession> ES;
        DataLayout DL;
        std::unique_ptr<TargetMachine> TM;
        MangleAndInterner Mangle;
        JITDylib &MainJD;
 
        /* Compilator layers */
        RTDyldObjectLinkingLayer ObjLayer;
        ObjectTransformLayer ObjTransformLayer;
        IRCompileLayer CompileLayer;
        IRTransformLayer TransformLayer;
    };
}}

