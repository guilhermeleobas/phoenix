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
  virtual void visit(phoenix::UnaryNode*) = 0;
  virtual void visit(phoenix::StoreNode*) = 0;
  virtual void visit(phoenix::BinaryNode*) = 0;
  virtual void visit(phoenix::TargetOpNode*) = 0;
  virtual void visit(phoenix::TerminalNode*) = 0;
  virtual void visit(phoenix::LoadNode*) = 0;
  virtual void visit(phoenix::ForeignNode*) = 0;
  virtual void visit(phoenix::ConstantNode*) = 0;
  virtual void visit(phoenix::ConstantIntNode*) = 0;

};
