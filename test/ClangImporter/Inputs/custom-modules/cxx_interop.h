#pragma once

// Ensure c++ features are used.
namespace ns {
class T {};
} // namespace ns

struct Basic {
  int a;
  ns::T *b;
};

Basic makeA();

namespace nested_struct_problem {

struct NamespacedStruct {
  int b;
};

}  // namespace nested_struct_problem

using NSStruct = nested_struct_problem::NamespacedStruct;
nested_struct_problem::NamespacedStruct* makeNamespacedStruct();

class Methods {
 public:
  virtual ~Methods();

  int SimpleMethod(int);

  int SimpleConstMethod(int) const;
  int some_value;
};
