#include "Compiler.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"


#include "time_measure.h"

#include <list>
#include <string>
#include <iostream>
#include <fstream>
#include <chrono>

using namespace llvm;
using namespace llvm::orc;

static cl::list<std::string> InputFiles(cl::Positional, cl::OneOrMore,
                                        cl::desc("input files"));

static cl::list<std::string> InputArgv("args", cl::Positional,
                                       cl::desc("<program arguments>..."),
                                       cl::ZeroOrMore, cl::PositionalEatsArgs);
static ExitOnError ExitOnErr;


void sort_function(Module& M)
{
  auto get_metric = [&](const StringRef name) {
    if(name.startswith("main_"))
      return 31;
    if(name.startswith("easy_"))
      return 0;
    return 30;
  };


  auto function_cmp = [&](const Function& lhs, const Function& rhs) {
    return get_metric(lhs.getName()) < get_metric(rhs.getName());
  };
  M.getFunctionList().sort(function_cmp);
}

std::unique_ptr<Module> load_module(int argc, char *argv[], LLVMContext& ctx)
{
  if(argc < 2) exit(1);

  auto InputFile = std::string(argv[1]);
  SMDiagnostic Err;
  auto M = parseIRFile(InputFile, Err, ctx);
  if (!M) {
    Err.print(argv[0], errs());
    exit(1);
  }

  return std::move(M);
}


void SplitModule_lambda(
    std::unique_ptr<Module> M, unsigned N,
    function_ref<void(std::unique_ptr<Module> MPart)> ModuleCallback,
    function_ref<unsigned(StringRef name)> getIndexCallback
    )
{
    for (unsigned I = 0; I < N; ++I) {
    ValueToValueMapTy VMap;
    std::unique_ptr<Module> MPart(
        CloneModule(*M, VMap, [&](const GlobalValue *GV) {
          return getIndexCallback(GV->getName()) == I;
        }));
    
    if (I != 0)MPart->setModuleInlineAsm("");

    ModuleCallback(std::move(MPart));
    }
}


void print_speed(size_t total_time, size_t n_function)
{
    double time = (double)total_time;
    double n = (double) n_function;

    time /= 1e+6; // convert into seconds

    std::cout << "Speed = " << n / time << " [f/s]" << std::endl;
}

    
#define divider do{for(int i = 0; i < 32; i++) printf("="); printf("\n");}while(0)



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
  auto ObjBuffer = std::make_unique<llvm::SmallVectorMemoryBuffer>(std::move(ObjBufferSV));
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





int main(int argc, char *argv[])
{
    InitLLVM X(argc, argv);
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    linkAllBuiltinGCs();

    //PassInstrumentationCallbacks PIC;
    //PassInstrumentation PI(&PIC);
    //TimePassesIsEnabled = true;
    
    SmallString<0> TimePassesStr;
    raw_svector_ostream ReportStream(TimePassesStr);

    // Load the IR inputs.
    auto ctx = std::make_unique<LLVMContext>();
    auto M = load_module(argc, argv, *ctx);
    sort_function(*M);

    enum {
      COMPILATOR,
      JIT,
      UNDEFINED
    } compilator_type;

    compilator_type = UNDEFINED;
    if(argc >= 3) {
      std::string option{argv[2]};
      if(option == "--compiler") {
        compilator_type = COMPILATOR;
      }
      if(option == "--jit") {
        compilator_type = JIT;
      }
    }

  if(compilator_type == UNDEFINED) {
    printf("Invalid compiler option\n");
    exit(1);
  }

  if(compilator_type == COMPILATOR && 0) {
    auto TM = std::move(get_tt());
    Compiler compilator(*TM);
    {
      ScopeTimer t___;

      llvm::SplitModule(std::move(M), 10, [&](std::unique_ptr<llvm::Module> part){
        t___.start();
        auto buffer = cantFail(compilator.compile(*part));
        t___.stop();
        t___.append();
      });

      //auto buffer = cantFail(compilator.compile(*M));

      double diff = (double) t___.total_time / 1e+6;
      std::cout << "Time diff = " << diff << std::endl;

      /*
      std::ofstream fs("out.o", std::ios::out | std::ios::binary);
      fs.write(buffer->getBufferStart(), buffer->getBufferSize());
      fs.close();
      */
    }

    divider;
    std::cout << "Compilator" << std::endl;
    divider;
  }

  if(compilator_type == COMPILATOR) {
    ScopeTimer t___;
    auto TM = std::move(get_tt());
    compile_module(*M, *TM);

    divider;
    std::cout << "Compilator" << std::endl;
    divider;
  }


  if(compilator_type == JIT) {
    // Create a JIT instance.
    auto jit = cantFail(JIT::Create());
    
    // Find all names of functions into module
    std::vector<std::string> func_names;
    for(auto& f : *M) {
        func_names.push_back(f.getName());
    }

    // Add module into jit queue
    ExitOnErr(jit->addModule(ThreadSafeModule(std::move(M), std::move(ctx))));

    {
    ScopeTimer t___;
    // Trigger compilation
    auto func = ExitOnErr(jit->lookup("f_0"));
    }
    
    divider;
    std::cout << "Jit" << std::endl;
    divider;


    // Show time destribution
    //reportAndResetTimings(&ReportStream);
  }
    
    return 0;
}
