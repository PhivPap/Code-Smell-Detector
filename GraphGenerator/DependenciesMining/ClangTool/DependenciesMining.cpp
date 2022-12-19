#include "DependenciesMining.h"
#include "Utilities.h"
#include "FileSystem.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/PreprocessorOptions.h"
#include <vector>
#include <unordered_set>
#include <iostream>
#include <atomic>

#define CLASS_DECL "ClassDecl"
#define STRUCT_DECL "StructDecl"
#define FIELD_DECL "FieldDecl"
#define METHOD_DECL "MethodDecl"
#define METHOD_VAR_OR_ARG "MethodVarOrArg"

namespace dependenciesMining {

using namespace filesystem;

// ----------------------------------------------------------------------------------------------

SymbolTable structuresTable;
Sources parsedFiles;
IgnoreRegistry ignored;

inline bool IsFilePathIgnored(const std::string& path) { return ignored["filePaths"]->isIgnored(path); }
inline bool IsNamespaceIgnored(const std::string& name) { return ignored["namespaces"]->isIgnored(name); }

// ----------------------------------------------------------------------------------------------

// Handle all the Classes and Structs and the Bases
void ClassDeclsCallback::run(const MatchFinder::MatchResult& result) {
	const CXXRecordDecl* d;

	Structure structure;

	if ((d = result.Nodes.getNodeAs<CXXRecordDecl>(CLASS_DECL))) {
		structure.SetStructureType(StructureType::Class);
	}
	else if ((d = result.Nodes.getNodeAs<CXXRecordDecl>(STRUCT_DECL))) {
		structure.SetStructureType(StructureType::Struct);
	}
	else {
		assert(0);
	}

	// gia ta declarations
	if (!(d->isCompleteDefinition())) {
		if (!d->hasDefinition()) {									// for templateDefinition Declarations only
			if (!d->getDescribedClassTemplate())
				return;
		}
		else {
			d = d->getDefinition();
		}
	}

	assert(d);

	const auto structID = GetIDfromDecl(d);

	if (isIgnoredDecl(d))
		return;

	// Templates
	if (d->getDescribedClassTemplate()) {
		structure.SetStructureType(StructureType::TemplateDefinition);
	}
	else if (d->getKind() == d->ClassTemplatePartialSpecialization) {
		structure.SetStructureType(StructureType::TemplatePartialSpecialization);
	}
	else if (d->getKind() == d->ClassTemplateSpecialization) {
		if (d->getTemplateSpecializationKind() == 1)
			structure.SetStructureType(StructureType::TemplateInstantiationSpecialization);
		else
			structure.SetStructureType(StructureType::TemplateFullSpecialization);
	}
	else if (d->getKind() == d->TemplateTemplateParm) {
		assert(0);
	}
	else if (d->getKind() == d->TemplateTypeParm) {
		assert(0);
	}

	const auto srcLocation = result.SourceManager->getPresumedLoc(d->getLocation());
	structure.SetSourceInfo(srcLocation.getFilename(), srcLocation.getLine(), srcLocation.getColumn());
	if (IsFilePathIgnored(srcLocation.getFilename())) {
		return;
	}

	//assert(structID); 
	structure.SetName(d->getNameAsString());
	structure.SetID(structID);

	// Namespace
	std::string fullEnclosingNamespace = GetFullNamespaceName(d);
	if (IsNamespaceIgnored(fullEnclosingNamespace)) {
		return;
	}
	structure.SetNamespace(fullEnclosingNamespace);

	// Templates 
	if (!d->hasDefinition()) {															// Templates that has Declaration only
		assert(structure.GetStructureType() == StructureType::TemplateDefinition);
		structure.SetStructureType(StructureType::Undefined);
		structuresTable.Install(structure.GetID(), structure);
		return;
	}

	if (d->getKind() == d->ClassTemplateSpecialization || d->getKind() == d->ClassTemplatePartialSpecialization) {
		// Template parent
		std::string parentName;
		ID_T parentID;

		Structure* templateParent{ nullptr };
		if (structure.IsTemplateInstantiationSpecialization()) {								// template Instantiation Specialization
			auto* parent = d->getTemplateInstantiationPattern();
			parentID = GetIDfromDecl(parent);
			parentName = GetFullStructureName(parent);
			templateParent = (Structure*)structuresTable.Lookup(parentID);
		}
		else {																					// template Full and Partial Specialization
			parentName = d->getQualifiedNameAsString();
			parentID = GetIDfromDecl(d);
			templateParent = (Structure*)structuresTable.Lookup(parentName);
		}

		if (!templateParent)
			templateParent = (Structure*)structuresTable.Install(parentID, parentName);

		structure.SetTemplateParent(templateParent);

		//Template Arguments		
		auto* temp = (ClassTemplateSpecializationDecl*)d;
		for (unsigned i = 0; i < temp->getTemplateArgs().size(); ++i) {
			const auto& templateArg = temp->getTemplateArgs()[i];
			TemplateArgsVisit(templateArg, [](TemplateArgument templateArg, Structure* structure) {
				RecordDecl* d = nullptr;
				if (templateArg.getKind() == TemplateArgument::Template) {
					d = (RecordDecl*)templateArg.getAsTemplateOrTemplatePattern().getAsTemplateDecl()->getTemplatedDecl();
					if (!d)													// a set of function templates 
						return;
				}

				if (d || GetTemplateArgType(templateArg)->isStructureOrClassType()) {
					if (!d)
						d = GetTemplateArgType(templateArg)->getAsCXXRecordDecl();
					std::string argStructName = GetFullStructureName(d);
					auto argStructID = GetIDfromDecl(d);
					Structure* argStruct = (Structure*)structuresTable.Lookup(argStructID);
					if (argStruct == nullptr)
						argStruct = (Structure*)structuresTable.Install(argStructID, argStructName);
					structure->InstallTemplateSpecializationArgument(argStructID, argStruct);
				}
				}, &structure);
		}
	}


	// Bases
	for (const auto& it : d->bases()) {
		auto* baseRecord = it.getType()->getAsCXXRecordDecl();
		if (baseRecord == nullptr)										// otan base einai template or partial specialization Ignored
			continue;
		std::string baseName = GetFullStructureName(baseRecord);
		auto baseID = GetIDfromDecl(baseRecord);
		Structure* base = (Structure*)structuresTable.Lookup(baseID);
		if (!base)
			base = (Structure*)structuresTable.Install(baseID, baseName);
		structure.InstallBase(baseID, base);
	}

	// Friends 
	for (auto* it : d->friends()) {
		auto* type = it->getFriendType();
		if (type) {																							// Classes
			auto parent = type->getType()->getAsCXXRecordDecl();
			if (!parent)																					// ignore decls that does not have definitions
				continue;
			auto parentID = GetIDfromDecl(parent);
			std::string parentName = GetFullStructureName(type->getType()->getAsCXXRecordDecl());
			Structure* parentStructure = (Structure*)structuresTable.Lookup(parentID);
			if (!parentStructure)
				parentStructure = (Structure*)structuresTable.Install(parentID, parentName);
			structure.InstallFriend(parentID, parentStructure);
		}
		else {																								// Methods			
			auto* decl = it->getFriendDecl();
			auto kind = decl->getKind();
			if (decl->getKind() == d->CXXMethod || decl->getKind() == d->FunctionTemplate) {
				CXXMethodDecl* methodDecl;
				if (decl->getKind() == d->FunctionTemplate) {												// Template Methods				 		
					auto funcdecl = ((FunctionTemplateDecl*)decl)->getTemplatedDecl();
					auto parent = funcdecl->getParent();
					if (!parent->isRecord())																	// Ignore Template Function
						continue;
					methodDecl = (CXXMethodDecl*)funcdecl;
				}
				else {
					methodDecl = (CXXMethodDecl*)decl;
				}
				std::string methodName = GetFullMethodName(methodDecl);
				auto* parentClass = methodDecl->getParent();
				auto parentClassID = GetIDfromDecl(parentClass);
				Structure* parentStructure = (Structure*)structuresTable.Lookup(parentClassID);
				if (!parentStructure) continue;
				// meta thn allagh se ids ws keys den krataw info gia to idio to method alla mono gia to structure pou anoikei
				structure.InstallFriend(parentClassID, parentStructure);
			}
			else if (decl->isTemplateDecl()) {																// Template Classes
				auto recdecl = (RecordDecl*)((TemplateDecl*)decl)->getTemplatedDecl();
				auto structureDefinition = recdecl->getDefinition();
				if (!structureDefinition)																	// ignore decls that does not have definitions
					continue;

				auto parentID = GetIDfromDecl(structureDefinition);
				auto parentName = GetFullStructureName(structureDefinition);
				Structure* parentStructure = (Structure*)structuresTable.Lookup(parentID);
				if (!parentStructure) {
					parentStructure = (Structure*)structuresTable.Lookup(parentName);
					if (!parentStructure) {
						parentStructure = (Structure*)structuresTable.Install(parentID, parentName);
					}
				}
				structure.InstallFriend(parentID, parentStructure);
			}
		}
	}

	// Members
	if (d->isCXXClassMember()) {
		const auto* parent = d->getParent();
		assert(parent);
		std::string parentName = GetFullStructureName((RecordDecl*)parent);
		auto parentID = GetIDfromDecl((RecordDecl*)parent);
		if (isIgnoredDecl((RecordDecl*)parent)) {
			return;
		}
		if (parentName != structure.GetName()) {
			Structure* parentStructure = (Structure*)structuresTable.Lookup(parentID);
			auto* inst = d->getInstantiatedFromMemberClass();
			structure.SetNestedParent(parentStructure);
			parentStructure->InstallNestedClass(structure.GetID(), (Structure*)structuresTable.Install(structure.GetID(), structure.GetName()));
		}
	}
	structuresTable.Install(structure.GetID(), structure);
}

// ----------------------------------------------------------------------------------------------
// Hanlde all the Fields in classes/structs (non structure fields)
void FeildDeclsCallback::installFundamentalField(const MatchFinder::MatchResult& result) {
	if (const FieldDecl* d = result.Nodes.getNodeAs<FieldDecl>(FIELD_DECL)) {
		const auto fieldID = GetIDfromDecl(d);

		auto* parent = d->getParent();

		// Ignored
		auto srcLocation = result.SourceManager->getPresumedLoc(d->getLocation());

		std::string parentName = GetFullStructureName(parent);
		std::string typeName = d->getType().getAsString();
		ID_T parentID = GetIDfromDecl(parent);

		Structure* parentStructure = (Structure*)structuresTable.Lookup(parentID);
		Definition field(fieldID, d->getNameAsString(), parentStructure->GetNamespace());
		field.SetSourceInfo(srcLocation.getFilename(), srcLocation.getLine(), srcLocation.getColumn());
		field.SetFullType(typeName);
		auto* _field = parentStructure->InstallField(fieldID, field);
		_field->SetAccessType((AccessType)d->getAccess());
	}
}


// Hanlde all the Fields in classes/structs (structure fields)
void FeildDeclsCallback::run(const MatchFinder::MatchResult& result) {
	if (const FieldDecl* d = result.Nodes.getNodeAs<FieldDecl>(FIELD_DECL)) {
		assert(d);

		const auto fieldID = GetIDfromDecl(d);

		const auto* parent = d->getParent();
		assert(parent);

		if (!parent->isClass() and !parent->isStruct())
			return;

		const auto srcLocation = result.SourceManager->getPresumedLoc(d->getLocation());
		// Ignored
		if (IsFilePathIgnored(srcLocation.getFilename())) {
			return;
		}

		if (IsNamespaceIgnored(GetFullNamespaceName(parent))) {
			return;
		}

		if (isIgnoredDecl(parent)) {
			return;
		}

		if (!isStructureOrStructurePointerType(d->getType())) {
			installFundamentalField(result);
			return;
		}

		if (parent->isClass() || parent->isStruct()) {
			std::string parentName = GetFullStructureName(parent);
			std::string typeName;
			ID_T parentID = GetIDfromDecl(parent);
			ID_T typeID = "";
			if (d->getType()->isPointerType() || d->getType()->isReferenceType()) {
				typeName = GetFullStructureName(d->getType()->getPointeeType()->getAsRecordDecl()); // CXX
				typeID = GetIDfromDecl(d->getType()->getPointeeType()->getAsRecordDecl());			// CXX
			}
			else {
				typeName = GetFullStructureName(d->getType()->getAsCXXRecordDecl());
				typeID = GetIDfromDecl(d->getType()->getAsCXXRecordDecl());
			}

			Structure* parentStructure = (Structure*)structuresTable.Lookup(parentID);
			Structure* typeStructure = (Structure*)structuresTable.Lookup(typeID);
			if (parentStructure->IsTemplateInstantiationSpecialization())		// insertion speciallization inherit its dependencies from the parent template
				return;
			if (!typeStructure)
				typeStructure = (Structure*)structuresTable.Install(typeID, typeName);

			Definition field(fieldID, d->getNameAsString(), parentStructure->GetNamespace(), typeStructure);
			field.SetSourceInfo(srcLocation.getFilename(), srcLocation.getLine(), srcLocation.getColumn());
			field.SetFullType(d->getType().getAsString());
			auto* _field = parentStructure->InstallField(fieldID, field);
			_field->SetAccessType((AccessType)d->getAccess());
		}
	}
}

// ----------------------------------------------------------------------------------------------

// Handle all the Methods
void MethodDeclsCallback::run(const MatchFinder::MatchResult& result) {
	if (const CXXMethodDecl* d = result.Nodes.getNodeAs<CXXMethodDecl>(METHOD_DECL)) {
		assert(d);
		
		const auto methodID = GetIDfromDecl(d);

		const RecordDecl* parent = d->getParent();
		std::string parentName = GetFullStructureName(parent);
		auto parentID = GetIDfromDecl(parent);

		if (!(d->isThisDeclarationADefinition())) {
			return;
		}

		const auto srcLocation = result.SourceManager->getPresumedLoc(d->getLocation());
		// Ignored
		if (IsFilePathIgnored(srcLocation.getFilename())) {
			return;
		}
		if (IsNamespaceIgnored(GetFullNamespaceName(parent))) {
			return;
		}

		if (isIgnoredDecl(parent)) {
			return;
		}

		Structure* parentStructure = (Structure*)structuresTable.Lookup(parentID);
		assert(parentStructure);
		Method method(methodID,  d->getNameAsString(), parentStructure->GetNamespace());
		method.SetSourceInfo(srcLocation.getFilename(), srcLocation.getLine(), srcLocation.getColumn());

		// Method's Type
		if (d->getDeclKind() == d->CXXConstructor) {
			if (d->isTrivial()) {
				method.SetMethodType(MethodType::Constructor_Trivial);
			}
			else {
				method.SetMethodType(MethodType::Constructor_UserDefined);
			}
		}
		else if (d->getDeclKind() == d->CXXDestructor) {
			if (d->isTrivial()) {
				method.SetMethodType(MethodType::Destructor_Trivial);
			}
			else {
				method.SetMethodType(MethodType::Destructor_UserDefined);
			}
		}
		else if (d->isOverloadedOperator()) {
			if (d->isTrivial()) {
				method.SetMethodType(MethodType::OverloadedOperator_Trivial);
			}
			else {
				method.SetMethodType(MethodType::OverloadedOperator_UserDefined);
			}
		}
		else if (d->getTemplatedKind()) {
			if (d->getTemplatedKind() == d->TK_FunctionTemplate) {
				method.SetMethodType(MethodType::TemplateDefinition);
			}
			else if (d->getTemplatedKind() == d->TK_FunctionTemplateSpecialization || d->getTemplatedKind() == d->TK_DependentFunctionTemplateSpecialization) {
				if (d->isTemplateInstantiation()) {
					method.SetMethodType(MethodType::TemplateInstantiationSpecialization);
				}
				else {
					method.SetMethodType(MethodType::TemplateFullSpecialization);
				}
			}
			else if (d->getTemplatedKind() == d->TK_MemberSpecialization) {
				method.SetMethodType(MethodType::UserMethod);		// user method on teplate class
			}
			else {
				std::cout << d->getTemplatedKind() << "\n\n";
				assert(0);
			}
		}
		else {
			method.SetMethodType(MethodType::UserMethod);
		}

		//Template
		if (method.IsTemplateFullSpecialization() || method.IsTemplateInstantiationSpecialization()) {
			//Template Arguments		
			auto args = d->getTemplateSpecializationArgs()->asArray();
			for (auto it : args) {
				TemplateArgsVisit(it, [](TemplateArgument templateArg, Method* method) {
					RecordDecl* d = nullptr;
					if (templateArg.getKind() == TemplateArgument::Template) {
						d = (RecordDecl*)templateArg.getAsTemplateOrTemplatePattern().getAsTemplateDecl()->getTemplatedDecl();
						if (!d)													// a set of function templates 
							return;
					}
					else if (templateArg.getKind() == TemplateArgument::Integral) {
						return;
					}
					if (d || GetTemplateArgType(templateArg)->isStructureOrClassType()) {
						if (!d)
							d = GetTemplateArgType(templateArg)->getAsCXXRecordDecl();
						std::string argStructName = GetFullStructureName(d);
						auto argStructID = GetIDfromDecl(d);
						//assert(argStructID);
						Structure* argStruct = (Structure*)structuresTable.Lookup(argStructID);
						if (argStruct == nullptr)
							argStruct = (Structure*)structuresTable.Install(argStructID, argStructName);
						method->InstallTemplateSpecializationArgument(argStructID, argStruct);
					}
					}, &method);
			}
		}

		//Return
		auto returnType = d->getReturnType();
		if (isStructureOrStructurePointerType(returnType)) {
			std::string typeName;
			ID_T typeID;
			if (returnType->isPointerType() || returnType->isReferenceType()) {
				typeName = GetFullStructureName(returnType->getPointeeType()->getAsCXXRecordDecl());
				typeID = GetIDfromDecl(returnType->getPointeeType()->getAsCXXRecordDecl());
			}
			else {
				typeName = GetFullStructureName(returnType->getAsCXXRecordDecl());
				typeID = GetIDfromDecl(returnType->getAsCXXRecordDecl());
			}
			Structure* typeStructure = (Structure*)structuresTable.Lookup(typeID);
			if (!typeStructure)
				typeStructure = (Structure*)structuresTable.Install(typeID, typeName);
			method.SetReturnType(typeStructure);
		}

		currentMethod = (Method*)parentStructure->InstallMethod(methodID, method);
		sm = result.SourceManager;

		// Body - MemberExpr
		auto* body = d->getBody();
		if (body == nullptr) {
			currentMethod = nullptr;
			return;
		}
		FindMemberExprVisitor visitor;

		literal_count = 0;
		statement_count = 0;
		branch_count = 0;
		loop_count = 0;
		scope_depth = -1;
		scope_max_depth = 0;

		visitor.TraverseStmt(body);

		currentMethod->SetAccessType((AccessType)d->getAccess());
		currentMethod->SetLiterals(literal_count);
		currentMethod->SetStatements(statement_count);
		currentMethod->SetBranches(branch_count);
		currentMethod->SetLoops(loop_count);
		currentMethod->SetMaxScopeDepth(scope_max_depth);
		currentMethod->SetLineCount(sm->getExpansionLineNumber(body->getEndLoc()) - sm->getExpansionLineNumber(body->getBeginLoc()));
		currentMethod->SetVirtual(d->isVirtual());
		currentMethod = nullptr;


	}
}

// ----------------------------------------------------------------------------------------------

bool MethodDeclsCallback::FindMemberExprVisitor::TraverseStmt(Stmt* stmt) {
	if (!stmt)
		return true;

	std::string class_name = stmt->getStmtClassName();

	switch (stmt->getStmtClass()) {
	case Stmt::StmtClass::CompoundStmtClass:
		MethodDeclsCallback::statement_count += std::distance(stmt->child_begin(), stmt->child_end());
		scope_depth++;
		if (scope_max_depth < scope_depth)
			scope_max_depth = scope_depth;
		break;

	case Stmt::StmtClass::IfStmtClass:
	case Stmt::StmtClass::SwitchStmtClass:
	case Stmt::StmtClass::BreakStmtClass:
	case Stmt::StmtClass::ContinueStmtClass:
	case Stmt::StmtClass::GotoStmtClass:
	case Stmt::StmtClass::ReturnStmtClass:
		MethodDeclsCallback::branch_count++;
		break;

	case Stmt::StmtClass::ForStmtClass:
	case Stmt::StmtClass::WhileStmtClass:
		MethodDeclsCallback::loop_count++;
		break;

	default: {
		if (class_name.find("Literal") != std::string::npos)
			MethodDeclsCallback::literal_count++;
	}
	}
	RecursiveASTVisitor<FindMemberExprVisitor>::TraverseStmt(stmt);
	if (stmt->getStmtClass() == Stmt::StmtClass::CompoundStmtClass)
		scope_depth--;
	return true;
}


// Handle all the MemberExpr in a method 
bool MethodDeclsCallback::FindMemberExprVisitor::VisitMemberExpr(MemberExpr* memberExpr) {
	auto* decl = memberExpr->getMemberDecl();
	auto type = memberExpr->getType();
	auto* base = memberExpr->getBase();

	if (base) {
		base = base->IgnoreUnlessSpelledInSource();				// clean all the invisble AST nodes that may surround this stmt

		if (base->getStmtClass() == memberExpr->CXXThisExprClass)	// ignore this
			return true;

		std::string str;
		Method::Member::MemberType memType;
		if (base->getStmtClass() == memberExpr->DeclRefExprClass) {
			str = "__LOCAL DEF__";
			memType = MethodDefinition_mem_t;
		}
		else if (base->getStmtClass() == memberExpr->MemberExprClass && ((MemberExpr*)base)->getBase()->getStmtClass() == memberExpr->CXXThisExprClass) {
			str = "__CLASS FIELD__";
			memType = ClassField_mem_t;
		}
		else if (base->getStmtClass() == memberExpr->CXXMemberCallExprClass) {
			str = "__CLASS METHOD__";
			memType = ClassMethod_mem_t;
		}
		else {
			str = "__??__";
			memType = Value_mem_t;
		}

		auto baseType = base->getType();
		auto baseRange = base->getSourceRange();
		auto baseScLocationBegin = MethodDeclsCallback::sm->getPresumedLoc(baseRange.getBegin());
		auto baseScLocationEnd = MethodDeclsCallback::sm->getPresumedLoc(baseRange.getEnd());
		SourceInfo baseLocBegin(baseScLocationBegin.getFilename(), baseScLocationBegin.getLine(), baseScLocationBegin.getColumn());
		SourceInfo baseLocEnd(baseScLocationEnd.getFilename(), baseScLocationEnd.getLine(), baseScLocationEnd.getColumn());
		std::string exprString = str;
		Method::MemberExpr methodMemberExpr("__DUMMY EXPR__", baseLocEnd, baseLocBegin.GetFileName(), baseLocBegin.GetLine(), baseLocBegin.GetColumn());

		std::string typeName;
		ID_T typeID;
		if (baseType->isPointerType() || baseType->isReferenceType()) {
			typeName = GetFullStructureName(baseType->getPointeeType()->getAsCXXRecordDecl());
			typeID = GetIDfromDecl(baseType->getPointeeType()->getAsCXXRecordDecl());
		}
		else {
			typeName = GetFullStructureName(baseType->getAsCXXRecordDecl());
			typeID = GetIDfromDecl(baseType->getAsCXXRecordDecl());
		}
		Structure* typeStructure = (Structure*)structuresTable.Lookup(typeID);
		if (!typeStructure)
			typeStructure = (Structure*)structuresTable.Install(typeID, typeName);

		Method::Member member(str, typeStructure, baseLocEnd, memType);
		MethodDeclsCallback::currentMethod->InsertMemberExpr(methodMemberExpr, member, baseLocBegin.toString());

	}

	auto range = memberExpr->getSourceRange();
	auto srcLocationBegin = MethodDeclsCallback::sm->getPresumedLoc(range.getBegin());
	auto srcLocationEnd = MethodDeclsCallback::sm->getPresumedLoc(range.getEnd());
	SourceInfo locBegin(srcLocationBegin.getFilename(), srcLocationBegin.getLine(), srcLocationBegin.getColumn());
	SourceInfo locEnd(srcLocationEnd.getFilename(), srcLocationEnd.getLine(), srcLocationEnd.getColumn());
	std::string exprString;

	Method::MemberExpr methodMemberExpr(exprString, locEnd, locBegin.GetFileName(), locBegin.GetLine(), locBegin.GetColumn());
	MethodDeclsCallback::currentMethod->UpdateMemberExpr(methodMemberExpr, locBegin.toString());

	return true;
}

// ----------------------------------------------------------------------------------------------

// Handle Method's Vars and Args
void MethodVarsCallback::run(const MatchFinder::MatchResult& result) {
	if (const VarDecl* d = result.Nodes.getNodeAs<VarDecl>(METHOD_VAR_OR_ARG)) {
		assert(d);

		const auto* parentMethodDecl = d->getParentFunctionOrMethod();

		// Ignore the methods declarations 
		if (!parentMethodDecl || !(((CXXMethodDecl*)parentMethodDecl)->isThisDeclarationADefinition()))
			return;

		const auto parentMethodID = GetIDfromDecl((CXXMethodDecl*)parentMethodDecl);

		const auto defID = GetIDfromDecl(d);

		// Ignore non method declarations
		if (!d->isLocalVarDeclOrParm() or parentMethodDecl->getDeclKind() != d->CXXMethod)
			return;

		auto* parentClass = (CXXRecordDecl*)parentMethodDecl->getParent();

		if (isIgnoredDecl(parentClass)) {
			return;
		}

		const auto srcLocation = result.SourceManager->getPresumedLoc(d->getLocation());

		if (IsFilePathIgnored(srcLocation.getFilename())) {
			return;
		}
		if (IsNamespaceIgnored(GetFullNamespaceName(parentClass))) {
			return;
		}

		auto parentClassName = GetFullStructureName(parentClass);
		auto parentMethodName = GetFullMethodName((CXXMethodDecl*)parentMethodDecl);
		auto parentClassID = GetIDfromDecl(parentClass);

		Structure* parentStructure = (Structure*)structuresTable.Lookup(parentClassID);
		Method* parentMethod = (Method*)parentStructure->LookupMethod(parentMethodID);

		std::string typeName;
		ID_T typeID;
		Definition* def = nullptr;

		if (isStructureOrStructurePointerType(d->getType())) {
			if (d->getType()->isPointerType() || d->getType()->isReferenceType()) {
				typeName = GetFullStructureName(d->getType()->getPointeeType()->getAsCXXRecordDecl());
				typeID = GetIDfromDecl(d->getType()->getPointeeType()->getAsCXXRecordDecl());
			}
			else {
				typeName = GetFullStructureName(d->getType()->getAsCXXRecordDecl());
				typeID = GetIDfromDecl(d->getType()->getAsCXXRecordDecl());
			}
			Structure* typeStructure = (Structure*)structuresTable.Lookup(typeID);
			if (!typeStructure)
				typeStructure = (Structure*)structuresTable.Install(typeID, typeName);
			def = new Definition(defID,  d->getNameAsString(), parentMethod->GetNamespace(), typeStructure);
			def->SetSourceInfo(srcLocation.getFilename(), srcLocation.getLine(), srcLocation.getColumn());
			def->SetFullType(typeName);
		}
		else {

			typeName = d->getType().getAsString();
			def = new Definition(defID, d->getNameAsString(), parentStructure->GetNamespace());
			def->SetSourceInfo(srcLocation.getFilename(), srcLocation.getLine(), srcLocation.getColumn());
			def->SetFullType(typeName);
		}

		if (d->isLocalVarDecl()) {
			parentMethod->InstallDefinition(defID, *def);
		}
		else {
			parentMethod->InstallArg(defID, *def);
		}

		delete def;
	}
}


// ----------------------------------------------------------------------------------------------

std::unique_ptr<ClangTool> CreateClangTool(const char* cmpDBPath, std::string& errorMsg) {
	assert(cmpDBPath);
	static auto database = CompilationDatabase::autoDetectFromSource(cmpDBPath, errorMsg);

#ifdef INCREMENTAL_GENERATION
	return database ? std::make_unique<ClangTool>(*database, DropParsedFiles(database->getAllFiles(), parsedFiles)) : nullptr;
#else
	return database ? std::make_unique<ClangTool>(*database, database->getAllFiles()) : nullptr;
#endif
}

std::unique_ptr<ClangTool> CreateClangTool(const char* cmpDBPath) {
	std::string ignoredErrorMsg;
	return CreateClangTool(cmpDBPath, ignoredErrorMsg);
}

std::unique_ptr<ClangTool> CreateClangTool(const std::vector<std::string>& srcs) {
	static llvm::cl::OptionCategory myToolCategory("my-tool options");
	static llvm::cl::extrahelp commonHelp(CommonOptionsParser::HelpMessage);
	static llvm::cl::extrahelp moreHelp("\nA help message for this specific tool can be added afterwards..\n");

	static auto argc = 3;
	const char* argv[3] = {"", "", "--"};
	static auto parser = CommonOptionsParser::create(argc, argv, myToolCategory);

#ifdef INCREMENTAL_GENERATION
	return parser ? std::make_unique<ClangTool>(parser->getCompilations(), DropParsedFiles(srcs, parsedFiles)) : nullptr;
#else
	return parser ? std::make_unique<ClangTool>(parser->getCompilations(), srcs) : nullptr;
#endif
}

void SetIgnoredRegions(const char* filesPath, const char* namespacesPath) {
	ignored["filePaths"] =  std::make_unique<IgnoredFilePaths>(filesPath);
	ignored["namespaces"] = std::make_unique<IgnoredNamespaces>(namespacesPath);
}

// NOTE:
// We use this anonymous namespace in order to mark source files that were bypassed in the AST due to disruptions from the GUI.
// These files must not be exported, since their contents have not been loaded into the native symbol table.
namespace {

	auto skippedFiles = std::unordered_set< std::string_view >();

	inline void MarkFileAsSkipped(std::string_view fileName) { skippedFiles.insert(fileName); }

	inline bool WasFileSkipped(std::string_view fileName) { return skippedFiles.find(fileName) != std::cend(skippedFiles); }

} // namespace

// NOTE:
// Usage of a clang::tooling::SourceFileCallbacks derivative, in order to signal the parsing of source files.
namespace {

	BeginSourceSignal beginSrcSignal;
	EndSourceSignal endSrcSignal;

	inline std::string_view GetCurrentFileName(const CompilerInstance& compiler) {
		auto parsed = SmallVector< const FileEntry* >();
		compiler.getSourceManager().getFileManager().GetUniqueIDMapping(parsed);
		assert(!parsed.empty());
		return parsed.back()->getName().data();
	}

	class SourceFileTracker : public SourceFileCallbacks {
	public:
		SourceFileTracker() = default;
		~SourceFileTracker() override = default;

		bool handleBeginSource(CompilerInstance& compiler) override {
			const auto currentFileName = GetCurrentFileName(compiler);

			if (!IsFilePathIgnored(std::string(currentFileName))) beginSrcSignal(currentFileName);
			if (IsMiningDisrupted()) MarkFileAsSkipped(currentFileName);

			return IsMiningDisrupted() ? false : true; // false breaks the AST recursion.
		}

		void handleEndSource() override { endSrcSignal(); }
	};

} // namespace

int MineArchitecture(ClangTool& tool) {
	assert(ignored.find("filePaths") != std::end(ignored) && "Make a call to SetIgnoredRegions()");
	assert(ignored.find("namespaces") != std::end(ignored) && "Make a call to SetIgnoredRegions()");

	static DeclarationMatcher classDeclMatcher = anyOf(cxxRecordDecl(isClass()).bind(CLASS_DECL), cxxRecordDecl(isStruct()).bind(STRUCT_DECL));
	static DeclarationMatcher fieldDeclMatcher = fieldDecl().bind(FIELD_DECL);
	static DeclarationMatcher methodDeclMatcher = cxxMethodDecl().bind(METHOD_DECL);
	static DeclarationMatcher methodVarMatcher = varDecl().bind(METHOD_VAR_OR_ARG);

	clang::CompilerInstance comp;
	comp.getPreprocessorOpts().addMacroDef("_W32BIT_");

	ClassDeclsCallback classCallback;
	FeildDeclsCallback fieldCallback;
	MethodDeclsCallback methodCallback;
	MethodVarsCallback methodVarCallback;
	MatchFinder finder;
	SourceFileTracker fileTracker;

	finder.addMatcher(classDeclMatcher, &classCallback);
	finder.addMatcher(fieldDeclMatcher, &fieldCallback);
	finder.addMatcher(methodDeclMatcher, &methodCallback);
	finder.addMatcher(methodVarMatcher, &methodVarCallback);

// NOTE: Need to add clang include directory as argument, else system headers won't be found, and the st will be incomplete on unix systems.
#ifdef __unix__
	static const auto clang_dir = "-I" + std::string(CLANG_INCLUDE_DIR);

	auto ardj1 = getInsertArgumentAdjuster(clang_dir.c_str());	
	tool.appendArgumentsAdjuster(ardj1);
#endif

  	return tool.run(newFrontendActionFactory(&finder, &fileTracker).get());
}

void GetMinedFiles(ClangTool& tool, std::vector<std::string>& srcs, std::vector<std::string>& headers) {
	assert(srcs.empty());
	assert(headers.empty());

#ifdef INCREMENTAL_GENERATION
	std::copy(std::begin(parsedFiles), std::end(parsedFiles) - 1, std::back_inserter(srcs)); // In order to include previously mined files.
#endif

	SmallVector<const FileEntry* > files;
	auto& file_manager = tool.getFiles();
	file_manager.GetUniqueIDMapping(files);

	for (auto* file : files) {
		const auto path =  file->getName().str();

		if (IsFilePathIgnored(path))
			continue;

		if (IsHeaderFile(path)) {
			headers.push_back(path);
		}
		else if (IsSourceFile(path) && !WasFileSkipped(path)) {
			srcs.push_back(path);
		}
	}
}

// ------------------------------------ //

Connection ConnectToBeginSource(const BeginSourceSlot& f) {
	return beginSrcSignal.connect(f);
}

Connection ConnectToEndSource(const EndSourceSlot& f) {
	return endSrcSignal.connect(f);
}

static std::atomic_bool disrupted { false };

bool IsMiningDisrupted(void) { return disrupted; }

void DisruptMining(void) { disrupted = true; }


} // dependenciesMining