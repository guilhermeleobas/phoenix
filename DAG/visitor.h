#pragma once

namespace phoenix{
	class Node;
	class UnaryNode;
  class StoreNode;

  class BinaryNode;
  class TargetOpNode;

  class TerminalNode;
  class LoadNode;
  class ForeignNode;
  class ConstantNode;
  class ConstantIntNode;
};

class Visitor{

 public:
  virtual void visit(phoenix::UnaryNode*){}
  virtual void visit(phoenix::StoreNode*){}
  virtual void visit(phoenix::BinaryNode*){}
  virtual void visit(phoenix::TargetOpNode*){}
  virtual void visit(phoenix::TerminalNode*){}
  virtual void visit(phoenix::LoadNode*){}
  virtual void visit(phoenix::ForeignNode*){}
  virtual void visit(phoenix::ConstantNode*){}
  virtual void visit(phoenix::ConstantIntNode*){}

};
