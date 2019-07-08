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

class Methods {
 public:
  virtual ~Methods();

  int SimpleMethod(int);

  int SimpleConstMethod(int) const;
  int some_value;
};
