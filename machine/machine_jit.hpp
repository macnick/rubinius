#ifndef RBX_MACHINE_JIT_HPP
#define RBX_MACHINE_JIT_HPP

#include <llvm/Config/llvm-config.h>

#if LLVM_VERSION_MAJOR >= 9
#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/IRTransformLayer.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#endif

#include "machine_threads.hpp"

#include <list>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace rubinius {
	namespace jit {

    class CompileRequest;

    typedef std::list<CompileRequest*> CompileList;

#if LLVM_VERSION_MAJOR >= 9

    using namespace llvm;
    using namespace llvm::orc;

    class Compiler {
			ExecutionSession ES;
			RTDyldObjectLinkingLayer ObjectLayer;
			IRCompileLayer CompileLayer;
			IRTransformLayer OptimizeLayer;

			DataLayout DL;
			MangleAndInterner Mangle;
			ThreadSafeContext Ctx;

			Compiler(JITTargetMachineBuilder JTMB, DataLayout DL)
        : ObjectLayer(ES, []() { return llvm::make_unique<SectionMemoryManager>(); })
        , CompileLayer(ES, ObjectLayer, ConcurrentIRCompiler(std::move(JTMB)))
        , OptimizeLayer(ES, CompileLayer, optimizeModule)
        , DL(std::move(DL)), Mangle(ES, this->DL)
        , Ctx(llvm::make_unique<LLVMContext>())
      {
        ES.getMainJITDylib().setGenerator(
            cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
                DL.getGlobalPrefix())));
      }

      virtual ~Compiler() { }

			const DataLayout &getDataLayout() const { return DL; }

			LLVMContext &getContext() { return *Ctx.getContext(); }

			static Expected<std::unique_ptr<Compiler>> Create() {
				auto JTMB = JITTargetMachineBuilder::detectHost();

				if (!JTMB)
					return JTMB.takeError();

				auto DL = JTMB->getDefaultDataLayoutForTarget();
				if (!DL)
					return DL.takeError();

				return llvm::make_unique<Compiler>(std::move(*JTMB), std::move(*DL));
			}

			Error addModule(std::unique_ptr<Module> M) {
				return OptimizeLayer.add(ES.getMainJITDylib(),
																 ThreadSafeModule(std::move(M), Ctx));
			}

			Expected<JITEvaluatedSymbol> lookup(StringRef Name) {
				return ES.lookup({&ES.getMainJITDylib()}, Mangle(Name.str()));
			}

		private:
			static Expected<ThreadSafeModule>
			optimizeModule(ThreadSafeModule TSM, const MaterializationResponsibility &R) {
				// Create a function pass manager.
				auto FPM = llvm::make_unique<legacy::FunctionPassManager>(TSM.getModule());

				// Add some optimizations.
				FPM->add(createInstructionCombiningPass());
				FPM->add(createReassociatePass());
				FPM->add(createGVNPass());
				FPM->add(createCFGSimplificationPass());
				FPM->doInitialization();

				// Run the optimizations over all functions in the module being added to
				// the JIT.
				for (auto &F : *TSM.getModule())
					FPM->run(F);

				return TSM;
			}
    };
#endif

		class MachineJIT : public MachineThread {
      CompileList list_;

      std::mutex list_mutex_;
      std::condition_variable list_condition_;

		public:
			MachineJIT(STATE);

      virtual ~MachineJIT() { }

      void initialize(STATE);
      void after_fork_child(STATE);
      void wakeup(STATE);
      void run(STATE);
		};
	}
}

#endif
