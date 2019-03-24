#pragma once

namespace phoenix{
	class Node;
	class UnaryNode;
  class BinaryNode;

  class TerminalNode;
  class ForeignNode;
  class ConstantNode;
  class ConstantIntNode;
};

class Visitor{

 public:
  virtual void visit(const phoenix::UnaryNode*){}
  virtual void visit(const phoenix::BinaryNode*){}
  virtual void visit(const phoenix::TerminalNode*){}
  virtual void visit(const phoenix::ForeignNode*){}
  virtual void visit(const phoenix::ConstantNode*){}
  virtual void visit(const phoenix::ConstantIntNode*){}

};
