#include "vm.hpp"
#include "state.hpp"
#include "machine_jit.hpp"
#include "environment.hpp"
#include "logger.hpp"

namespace rubinius {
  namespace jit {
    MachineJIT::MachineJIT(STATE)
      : MachineThread(state, "rbx.jit", MachineThread::eXLarge)
      , list_()
      , list_mutex_()
      , list_condition_()
    {
    }

    void MachineJIT::initialize(STATE) {
      MachineThread::initialize(state);
    }

    void MachineJIT::wakeup(STATE) {
      MachineThread::wakeup(state);

      list_condition_.notify_one();
    }

    void MachineJIT::after_fork_child(STATE) {
      MachineThread::after_fork_child(state);

      list_.clear();
    }

    void MachineJIT::run(STATE) {
      state->vm()->unmanaged_phase(state);

      while(!thread_exit_) {
        CompileRequest* request = 0;

        {
          std::unique_lock<std::mutex> lk(list_mutex_);
          list_condition_.wait(lk,
              [this]{ return thread_exit_ || !list_.empty(); });

          if(!list_.empty()) {
            request = list_.back();
            list_.pop_back();
          }
        }
      }
    }
  }
}
