#pragma once
#include "SymbolTable.h"
#include "STVisitor.h"
#include "Graph.h"

// Dependency Types
#define Inherit_dep_t "Inherit"
#define Friend_dep_t "Friend"
#define NestedClass_dep_t "NestedClass"
#define ClassField_dep_t "ClassField"
#define ClassTemplateParent_dep_t "ClassTemplateParent"
#define ClassTemplateArg_dep_t "ClassTemplateArg"
#define MethodReturn_dep_t "MethodReturn"
#define MethodArg_dep_t "MethodArg"
#define MethodDefinition_dep_t "MethodDefinition"
#define MemberExpr_dep_t "MemberExpr"
//#define MemberExpr_Value_dep_t "MemberExprValue"
//#define MemberExpr_ClassField_dep_t "MemberExprClassField"
//#define MemberExpr_MethodDefinition_dep_t "MemberExprMethodDefinition"
#define MethodTemplateArg_dep_t "MethodTemplateArgs"

using namespace graph;
using namespace dependenciesMining;

namespace graphGeneration {

	class GraphGenerationSTVisitor : public STVisitor {
		Graph graph;
		Node* currNode = nullptr;
		untyped::Object innerObj;
		Edge::DependencyType currDepType = Undefined_dep_t;
	public:
		virtual void VisitStructure(Structure* s);
		virtual void VisitMethod(Method* m);
		virtual void VisitDefinition(Definition* s);
		Graph& GetGraph();
	};

	Graph GenerateDependenciesGraph(const SymbolTable& st);
}