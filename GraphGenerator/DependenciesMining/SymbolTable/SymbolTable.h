#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <map>
#include <list>
#include <cassert>
#include <vector>
#include <algorithm>
#include "json/writer.h"

#define ID_T std::string 

namespace dependenciesMining {

	class SymbolTable;
	class Structure;
	class Definition; 
	class Method;
	class STVisitor;

	enum class StructureType {
		Undefined = -1,
		Class,
		Struct,
		TemplateDefinition,
		TemplateFullSpecialization,
		TemplateInstantiationSpecialization,
		TemplatePartialSpecialization
	};

	enum class MethodType {
		Undefined = -1,
		Constructor_Trivial,
		OverloadedOperator_Trivial,
		Destructor_Trivial,
		Constructor_UserDefined,
		Destructor_UserDefined,
		UserMethod,
		OverloadedOperator_UserDefined,
		TemplateDefinition,
		TemplateFullSpecialization,
		TemplateInstantiationSpecialization
	};


	enum class ClassType {
		Undefined = -1,
		Structure,
		Method, 
		Definition
	};

	enum class AccessType {
		unknown = -1,
		_public,
		_protected,
		_private
	};

	class SourceInfo {
	private:
		std::string fileName = "";
		int line = -1;
		int column = -1;
	public:
		SourceInfo() = default;
		SourceInfo(const std::string& fileName, int line, int column) : fileName(fileName), line(line), column(column) {};
		const std::string& GetFileName() const;
		int GetLine() const;
		int GetColumn() const;
		std::string toString() const;
		void SetFileName(const std::string& name);
		void SetLine(int line);
		void SetColumn(int column);
		void SetTotalLocation(const std::string& fileName, int line, int column);

		bool  operator<(SourceInfo const& loc) const;
		bool  operator<=(SourceInfo const& loc) const;
		bool  operator>(SourceInfo const& loc) const;
		bool  operator>=(SourceInfo const& loc) const;
		bool  operator==(SourceInfo const& loc) const;		
	};

	inline bool operator!=(const SourceInfo& lhs, const SourceInfo& rhs) {
		return !(lhs == rhs);
	}

	// ----------------------------------------------------------------------------------------

	class Symbol {
	protected:
		ID_T id = "";
		std::string name;
		std::string nameSpace = "";
		SourceInfo srcInfo;
		ClassType classType = ClassType::Undefined;
		AccessType access_type = AccessType::unknown;

	public:
		Symbol() = default; 
		Symbol(ClassType classType) : classType(classType) {};
		Symbol(const ID_T& id, const std::string& name, const std::string& nameSpace = "", ClassType classType = ClassType::Undefined, const std::string& fileName = "", int line = -1, int column = -1)
			: id(id), name(name), nameSpace(nameSpace), classType(classType), srcInfo(fileName, line, column) {};
		
		virtual ~Symbol() = default;

		virtual const ID_T& GetID() const;
		virtual const std::string& GetName() const;
		virtual ClassType GetClassType() const;
		virtual const char* GetClassTypeAsString() const;
		virtual const SourceInfo& GetSourceInfo() const;
		virtual const std::string& GetNamespace() const;
		const char* GetAccessTypeStr() const;
		AccessType GetAccessType() const;

		virtual void SetID(const ID_T& id);
		virtual void SetName(const std::string& name);
		virtual void SetClassType(const ClassType& type);
		virtual void SetSourceInfo(const SourceInfo& info);
		virtual void SetSourceInfo(const std::string& fileName, int line, int column);
		virtual void SetNamespace(const std::string& nameSpace);
		void SetAccessType(const AccessType& access_type);
	};

	// ----------------------------------------------------------------------------------------

	class SymbolTable {
	private:
		std::unordered_map<ID_T, Symbol*> byID;
		std::unordered_map<std::string, std::list<Symbol*>> byName;
	public:
		using Size = std::unordered_map<ID_T, Symbol*>::size_type;

		bool IsEmpty() const { return byID.empty(); }
		Size GetSize() const { return byID.size(); }

		Symbol* Install(const ID_T& id, const std::string& name, const ClassType& type = ClassType::Structure);		// TO FIX
		Symbol* Install(const ID_T& id, const Symbol& symbol);
		Symbol* Install(const ID_T& id, const Structure& symbol);
		Symbol* Install(const ID_T& id, const Method& symbol);
		Symbol* Install(const ID_T& id, const Definition& symbol);
		Symbol* Install(const ID_T& id, Symbol* symbol);

		Symbol* Install2(const ID_T& id, const Structure& symbol); // cause of Install implementation, didnt want to break anything

		Symbol* Lookup(const ID_T& id);
		//Symbol* Lookup(const std::string& name);
		const Symbol* Lookup(const ID_T& id) const;
		//const Symbol* Lookup(const std::string& name) const;

		void Print();
		void Print2(int level);

		Json::Value GetJsonStructure(Structure* structure);
		Json::Value GetJsonMethod(Method* method);
		Json::Value GetJsonDefinition(Definition* definition);

		void AddJsonStructure(Structure* structure, Json::Value& json_structure) const;
		void AddJsonMethod(Method* method, Json::Value& json_method) const;
		void AddJsonDefinition(Definition* definition, Json::Value& json_definition) const;
		void AddJsonSymbolTable(Json::Value& st) const;

		Json::Value GetJsonSymbolTable(void);

		void Accept(STVisitor* visitor);
		void Accept(STVisitor* visitor) const;

		using iterator = std::unordered_map <ID_T, Symbol*>::iterator;
		using const_iterator = std::unordered_map <ID_T, Symbol*>::const_iterator;

		iterator begin() { return byID.begin(); }

		const_iterator begin() const { return byID.begin(); }

		iterator end() { return byID.end(); }

		const_iterator end() const { return byID.end(); }
	};

	// ----------------------------------------------------------------------------------------

	template<typename Parent_T> class Template {
	private:
		Parent_T* parent = nullptr;
		SymbolTable arguments;

	public:
		Template() = default;

		Parent_T* GetParent() const;
		const SymbolTable& GetArguments() const;
		void SetParent(Parent_T* structure); 
		Symbol* InstallArgument(const ID_T& id, Structure* structure);
	};

	// ----------------------------------------------------------------------------------------

	class Definition : public Symbol {
	private:
		Structure* type = nullptr;	// Definition type, can be nullptr in case of non-user-defined types.
		std::string full_type = "";	// Full definition type.

	public:
		Definition() : Symbol(ClassType::Definition) {};
		Definition(const ID_T& id, const std::string& name, const std::string& nameSpace = "", Structure* type = nullptr)
			: Symbol(id, name, nameSpace, ClassType::Definition), type(type) {};
		Definition(const ID_T& id, const std::string& name, const std::string& nameSpace, Structure* type, const std::string& fileName, int line, int column)
			: Symbol(id, name, nameSpace, ClassType::Definition, fileName, line, column), type(type) {};

		virtual ~Definition() override = default;

		bool isStructure() const;
		const Structure* GetType() const;
		const std::string& GetFullType() const;
		void SetType(Structure* structure);
		void SetFullType(const std::string& type);
	};

	#define Value_mem_t "Value"
	#define ClassField_mem_t "ClassField"
	#define ClassMethod_mem_t "ClassMethod"
	#define MethodDefinition_mem_t "MethodDefinition"

	class Method : public Symbol {
	public:
		class Member {
		public:
		using MemberType = std::string; 
		private:
			std::string name;
			SourceInfo locEnd;
			Structure* type;
			MemberType memType = Value_mem_t;

		public:
			Member() = default;
			Member(const std::string& name, Structure* type, SourceInfo locEnd, MemberType memType = Value_mem_t) : name(name), type(type), locEnd(locEnd), memType(memType) {};
			const std::string& GetName() const;
			SourceInfo GetLocEnd() const;
			Structure* GetType() const;
			MemberType GetMemberType() const;

			void SetName(const std::string& name);
			void SetLocEnd(const SourceInfo& locEnd);
			void SetType(Structure* type);
		};

		class MemberExpr {
			std::string expr;
			SourceInfo srcInfo;									// srcInfo: expr position on src code		
			SourceInfo locEnd;									// to store the longer expr (as string)
			std::vector<Member> members;
		public:
			MemberExpr() = default;
			MemberExpr(std::string expr, SourceInfo locEnd, std::string fileName, int line, int column) : expr(expr), locEnd(locEnd), srcInfo(SourceInfo(fileName, line, column)) {};
			std::string GetExpr() const;
			std::vector<Member> GetMembers() const;
			SourceInfo GetLocEnd() const;
			SourceInfo GetSourceInfo() const;
			void SetExpr(std::string expr);
			void SetLocEnd(SourceInfo locEnd);
			void InsertMember(Member member);
		};

	private:
		MethodType methodType = MethodType::Undefined;
		Structure* returnType = nullptr;
		Template<Method> templateInfo;								// Template Parent is *not* used
		SymbolTable arguments;
		SymbolTable definitions;
		std::map<std::string, MemberExpr> memberExprs;	// <location, MemberExpr>
		int literals = 0;
		int statements = 0;
		int branches = 0;
		int loops = 0;
		int max_scope_depth = 0;
		int line_count = 0;
		bool is_virtual;
	
	public:
		Method() : Symbol(ClassType::Method) {};
		Method(const ID_T& id, const std::string& name, const std::string& nameSpace = "") : Symbol(id, name, nameSpace, ClassType::Method) {};
		Method(const ID_T& id, const std::string& name, const std::string& nameSpace, const std::string& fileName, int line, int column)
			: Symbol(id, name, nameSpace, ClassType::Method, fileName, line, column) {};

		virtual ~Method() override = default;
		
		MethodType GetMethodType() const;
		const char* GetMethodTypeAsString() const;
		Structure* GetReturnType() const;

		const SymbolTable& GetArguments() const;
		const SymbolTable& GetDefinitions() const;
		const SymbolTable& GetTemplateArguments() const;
		const std::map<std::string, MemberExpr>& GetMemberExpr() const;
		int GetLiterals() const;
		int GetStatements() const;
		int GetBranches() const;
		int GetLoops() const;
		int GetMaxScopeDepth() const;
		int GetLineCount() const;

		void SetMethodType(const MethodType& type);
		void SetReturnType(Structure* structure);
		void SetTemplateParent(Method* structure);
		void SetLiterals(int literals);
		void SetStatements(int statements);
		void SetBranches(int branches);
		void SetLoops(int loops);
		void SetMaxScopeDepth(int max_scope_depth);
		void SetLineCount(int line_count);
		void SetVirtual(bool is_virtual);

		Symbol* InstallArg(const ID_T& id, const Definition& definition);
		Symbol* InstallDefinition(const ID_T& id, const Definition& definition);
		Symbol* InstallTemplateSpecializationArgument(const ID_T& id, Structure* structure);

		void InsertMemberExpr(MemberExpr const& memberExpr, Member const& member, const std::string& locBegin);
		void UpdateMemberExpr(MemberExpr const& memberExpr, const std::string& locBegin);

		bool IsConstructor () const;
		bool IsDestructor() const;
		bool IsUserMethod() const;
		bool IsOverloadedOperator() const;
		bool IsTemplateDefinition() const;
		bool IsTemplateFullSpecialization() const;
		bool IsTemplateInstantiationSpecialization() const;
		bool IsTrivial() const;
		bool IsVirtual() const;
	};

	// ----------------------------------------------------------------------------------------

	class Structure : public Symbol {
	private:
		
		StructureType structureType = StructureType::Undefined;
		Template<Structure> templateInfo;
		Structure* nestedParent = nullptr;
		SymbolTable methods; 
		SymbolTable fields; 
		SymbolTable bases;
		SymbolTable contains; 
		SymbolTable friends;	// About Structures: Key->structureID, Value->Structure*
								// About Methods: Key->methodID, Value->Structure* (the parent Class which owns this method)
	
	public:
		Structure() : Symbol(ClassType::Structure) {};
		Structure(const ID_T& id) : Symbol{id, id, "",  ClassType::Structure} {}
		Structure(const ID_T& id, const std::string& name, const std::string& nameSpace = "", StructureType structureType = StructureType::Undefined)
			: Symbol(id, name, nameSpace, ClassType::Structure), structureType(structureType) {};
		Structure(const ID_T& id, const std::string& name, const std::string& nameSpace, StructureType structureType, const std::string& fileName, int line, int column)
			: Symbol(id, name, nameSpace, ClassType::Structure, fileName, line, column), structureType(structureType) {};
		Structure(const Structure& s); 

		virtual ~Structure() override = default;
		
		StructureType GetStructureType() const;
		const char* GetStructureTypeAsString() const;
		Structure* GetTemplateParent() const;
		Structure* GetNestedParent() const;

		const SymbolTable& GetMethods() const; 
		const SymbolTable& GetFields() const; 
		const SymbolTable& GetBases() const; 
		const SymbolTable& GetContains() const;
		const SymbolTable& GetFriends() const;
		const SymbolTable& GetTemplateArguments() const; 

		void SetStructureType(const StructureType& structureType);
		void SetTemplateParent(Structure* structure);
		void SetNestedParent(Structure* structure);

		Symbol* LookupMethod(const ID_T& id);

		Symbol* InstallMethod(const ID_T& id, const Method& method);
		Symbol* InstallField(const ID_T& id, const Definition& definition);
		Symbol* InstallBase(const ID_T& id, Structure* structure);
		Symbol* InstallNestedClass(const ID_T& id, Structure* structure);
		Symbol* InstallFriend(const ID_T& id, Structure* structure);
		Symbol* InstallTemplateSpecializationArgument(const ID_T& id, Structure* structure);

		bool IsTemplateDefinition() const;
		bool IsTemplateFullSpecialization() const;
		bool IsTemplateInstantiationSpecialization() const;
		bool IsTemplatePartialSpecialization() const;
		bool IsTemplate() const;
		bool IsUndefined() const;
		bool IsNestedClass() const;
	};

	/*class Fundamental : public Symbol {

	};*/

}