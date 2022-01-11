#pragma once

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

#include "llvm/Transforms/Utils/SplitModule.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"


namespace llvm { namespace orc {
    class JIT {
      public:
        static Expected<std::unique_ptr<JIT>> Create() {
            auto JTMB = orc::JITTargetMachineBuilder::detectHost();
            if (!JTMB)
                return JTMB.takeError();
            JTMB->setCodeGenOptLevel(CodeGenOpt::Default);

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
                         std::move(ES), std::move(*DL), std::move(*JTMB),
                         std::move(*ProcessSymbolsSearchGenerator)
                     )
                );
            return std::move(jit);
        }
        

        ExecutionSession &getES() {return *ES; };
        Error addModule(ThreadSafeModule TSM) { return CompileLayer.add(MainJD, std::move(TSM)); }
        Expected<JITEvaluatedSymbol> lookup(StringRef name) { return ES->lookup({&MainJD}, Mangle(name)); }
      private:
        /* Constructor */
        JIT(
            std::unique_ptr<ExecutionSession> ES,
            DataLayout DL,
            orc::JITTargetMachineBuilder JTMB,
            std::unique_ptr<DynamicLibrarySearchGenerator> ProcessSymbolsGenerator
        ) :
            ES(std::move(ES)),
            DL(std::move(DL)),
            Mangle(*this->ES, this->DL),
            MainJD(this->ES->createJITDylib("<main>")),
            ObjLayer(*this->ES, createMM),
            ObjTransformLayer(*this->ES, ObjLayer, objectTransformation),
            CompileLayer(*this->ES, ObjTransformLayer, std::make_unique<ConcurrentIRCompiler>(std::move(JTMB)))
        {
            MainJD.addGenerator(std::move(ProcessSymbolsGenerator));
        }

      private:

        static std::unique_ptr<SectionMemoryManager> createMM() { return std::make_unique<SectionMemoryManager>(); }

        static std::unique_ptr<MemoryBuffer> objectTransformation(std::unique_ptr<MemoryBuffer> input_buffer)
        {
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
        MangleAndInterner Mangle;
        JITDylib &MainJD;
 
        /* Compilator layers */
        RTDyldObjectLinkingLayer ObjLayer;
        ObjectTransformLayer ObjTransformLayer;
        IRCompileLayer CompileLayer; 
    };
}}

