#include "instructions.hpp"

namespace rubinius {
  namespace instructions {
    inline void push_type(STATE, CallFrame* call_frame) {
      stack_push(G(type));
    }
  }
}
