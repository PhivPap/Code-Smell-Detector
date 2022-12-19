// Incremental Generation module.
// Soultatos Stefanos 2022

#include "Incremental.h"
#include "Converters.h"

#include <algorithm>
#include <filesystem>

namespace incremental {

	using namespace dependenciesMining;
	using namespace details;

	namespace {

		using JsonArchive = std::ifstream;
		using JsonVal = Json::Value;
		using JsonString = Json::String;
		using SymbolID = JsonString;
		using namespace dependenciesMining;

		// -------------------------- Json Utilities --------------------------------------- //

		// Utility for safe json access.
		inline auto Get(const JsonVal& val, const char* at) { 
			assert(val.isMember(at));
			return val[at];
		}

		// Utility for id based json iteration.
		template <typename Function>
		void ForEach(const JsonVal& val, Function f) {
			static_assert(std::is_invocable_v<Function, const JsonString&, const JsonVal&>, "Invalid function.");

			for (auto iter = std::begin(val); iter != std::end(val); ++iter)
				f(iter.name(), *iter);
		}

		// --------------------------------------------------------------------------------- //

		// -------------------------- Deserializers ---------------------------------------- //

		inline SourceInfo DeserializeSrcInfo(const JsonVal& val) {
			return { Get(val, "file").asString(), Get(val, "line").asInt(), Get(val, "col").asInt() };
		}

		inline void DeserializeDefinition(const SymbolID& id, const JsonVal& val, Definition* d) {
			d->SetID(id);
			d->SetName(Get(val, "name").asString());
			d->SetFullType(Get(val, "full_type").asString());
			d->SetSourceInfo(DeserializeSrcInfo(Get(val, "src_info")));
		}

		inline void DeserializeDefinitionWithAccessSpecifier(const SymbolID& id, const JsonVal& val, Definition* d) {
			DeserializeDefinition(id, val, d);
			d->SetAccessType(ToAccessType(Get(val, "access").asString()));
		}

		inline void DeserializeMethod(const SymbolID& id, const JsonVal& val, Method* m) {
			m->SetID(id);
			m->SetName(Get(val, "name").asString());
			m->SetBranches(Get(val, "branches").asInt());
			m->SetLineCount(Get(val, "lines").asInt());
			m->SetLiterals(Get(val, "literals").asInt());
			m->SetLoops(Get(val, "loops").asInt());
			m->SetMaxScopeDepth(Get(val, "max_scope").asInt());
			m->SetAccessType(ToAccessType(Get(val, "access").asString()));
			m->SetSourceInfo(DeserializeSrcInfo(Get(val, "src_info")));
			m->SetStatements(Get(val, "statements").asInt());
			m->SetVirtual(Get(val, "virtual").asBool());
			m->SetMethodType(ToMethodType(Get(val, "method_type").asString()));
		}

		inline void DeserializeStructure(const SymbolID& id, const JsonVal& val, Structure* s) {
			s->SetID(id);
			s->SetName(Get(val, "name").asString());
			s->SetNamespace(Get(val, "namespace").asString());
			s->SetStructureType(ToStructureType(Get(val, "structure_type").asString()));
			s->SetSourceInfo(DeserializeSrcInfo(Get(val, "src_info")));
		}

		// --------------------------------------------------------------------------------- //

		// --------------------------- Generic Dependency Installers ----------------------- //

		template <typename Installer>
		void InstallDependency(const JsonVal& val, SymbolTable& table, Installer install) {
			static_assert(std::is_invocable_v<Installer, Structure*>, "Invalid installer.");

			assert(!val.isArray());

			if (val.isNull()) {
				install((Structure*) nullptr);
			}
			else {
				assert(val.isString());

				const auto id = val.asString();

				install((Structure*) table.Install2(id, Structure{ id }));
			}
		}

		template <typename Installer>
		void InstallDependencies(const JsonVal& val, SymbolTable& table, Installer install) {
			static_assert(std::is_invocable_v<Installer, const SymbolID&, Structure*>, "Invalid installer.");

			for (const auto& v : val) {
				assert(v.isString());

				const auto id = v.asString();

				install(id, (Structure*) table.Install2(id, Structure{ id }));
			}
		}

		// --------------------------------------------------------------------------------- //

		// ------------------------------ Importers/Installers ----------------------------- //

		Structure* ImportStructure(const SymbolID& id, const JsonVal& val, SymbolTable& table); // fwd declare for early usage

		inline void InstallStructureNestedClasses(const JsonVal& val, SymbolTable& table, Structure* s) {
			InstallDependencies(Get(val, "contains"), table, [s](const auto& id, auto* nested) {
				s->InstallNestedClass(id, nested); 
			});
		}

		inline void InstallStructureFields(const JsonVal& val, SymbolTable& table, Structure* s) {
			ForEach(Get(val, "fields"), [s, &table](const auto& id, const auto& val) { 
				auto* f = (Definition*) s->InstallField(id, {});
				assert(f);

				DeserializeDefinitionWithAccessSpecifier(id, val, f);

				InstallDependency(Get(val, "type"), table, [f](auto* t) {
					f->SetType(t);
				});

				f->SetNamespace(s->GetNamespace());
			});
		}

		inline void InstallMethodReturnType(const JsonVal& val, SymbolTable& table, Method* m) {
			assert(!Get(val, "ret_type").isNull());
			assert(Get(val, "ret_type").isString());

			const auto id = Get(val, "ret_type").asString();

			m->SetReturnType(id == "void" ? nullptr : (Structure*) table.Install2(id, Structure{ id }));
		}
		
		inline void InstallMethodArgs(const JsonVal& val, SymbolTable& table, Method* m) {
			ForEach(Get(val, "args"), [m, &table](const auto& id, const auto& val) { 
				auto* d = (Definition*) m->InstallArg(id, {});
				assert(d);

				DeserializeDefinition(id, val, d);

				InstallDependency(Get(val, "type"), table, [d](auto* t) {
					d->SetType(t);
				});

				d->SetNamespace(m->GetNamespace());
			});
		}

		inline void InstallMethodDefinitions(const JsonVal& val, SymbolTable& table, Method* m) {
			ForEach(Get(val, "definitions"), [m, &table](const auto& id, const auto& val) {
				auto* d = (Definition*) m->InstallDefinition(id, {});
				assert(d);

				DeserializeDefinition(id, val, d);

				InstallDependency(Get(val, "type"), table, [d](auto* t) {
					d->SetType(t);
				});

				d->SetNamespace(m->GetNamespace());
			});
		}

		inline void InstallMethodTemplateArgs(const JsonVal& val, SymbolTable& table, Method* m) {
			InstallDependencies(Get(val, "template_args"), table, [m](const auto& id, auto* arg) {
				m->InstallTemplateSpecializationArgument(id, arg);
			});
		}

		inline void InstallStructureFriends(const JsonVal& val, SymbolTable& table, Structure* s) {
			InstallDependencies(Get(val, "friends"), table, [s](const auto& id, auto* f) {
				assert(f);

				s->InstallFriend(id, f);
			});
		}

		inline void InstallStructureMethods(const JsonVal& val, SymbolTable& table, Structure* s) {
			ForEach(Get(val, "methods"), [&table, s](const auto& id, const auto& val) {
				auto* m = (Method*) s->InstallMethod(id, {});
				assert(m);

				DeserializeMethod(id, val, m);

				m->SetNamespace(s->GetNamespace());

				InstallMethodReturnType(val, table, m);
				InstallMethodArgs(val, table, m);
				InstallMethodDefinitions(val, table, m);
				InstallMethodTemplateArgs(val, table, m);
			});
		}

		inline void InstallStructureBases(const JsonVal& val, SymbolTable& table, Structure* s) {
			InstallDependencies(Get(val, "bases"), table, [s](const auto& id, auto* base) {
				assert(base);

				s->InstallBase(id, base);
			});
		}

		inline void InstalllStructureTemplateParent(const JsonVal& val, SymbolTable& table, Structure* s) {
			InstallDependency(Get(val, "template_parent"), table, [s](auto* parent) { 
				s->SetTemplateParent(parent); 
			});
		}

		inline void InstalllStructureNestedParent(const JsonVal& val, SymbolTable& table, Structure* s) {
			InstallDependency(Get(val, "nested_parent"), table, [s](auto* parent) { 
				s->SetNestedParent(parent); 
			});
		}

		inline void InstallStructureTemplateArguments(const JsonVal& val, SymbolTable& table, Structure* s) {
			InstallDependencies(Get(val, "template_args"), table, [s](const auto& id, auto* arg) {
				assert(arg);
				s->InstallTemplateSpecializationArgument(id, arg);
			});
		}

		Structure* ImportStructure(const SymbolID& id, const JsonVal& val, SymbolTable& table) {
			auto* s = (Structure*) table.Install2(id, Structure{}); // Might return structure that was installed as a dependency.
			assert(s);

			DeserializeStructure(id, val, s);

			InstallStructureNestedClasses(val, table, s);
			InstallStructureBases(val, table, s);
			InstallStructureFields(val, table, s);
			InstallStructureFriends(val, table, s);
			InstallStructureMethods(val, table, s);
			InstalllStructureNestedParent(val, table, s);
			InstalllStructureTemplateParent(val, table, s);
			InstallStructureTemplateArguments(val, table, s);

			return s;
		}

		inline void ImportStructures(const JsonVal& val, SymbolTable& table) {
			ForEach(Get(val, "structures"), [&table](const auto& id, const auto& val) { 
				ImportStructure(id, val, table); 
			});
		}

		// --------------------------------------------------------------------------------- //

		inline JsonVal GetArchiveRoot(JsonArchive& from) {
			JsonVal root;
			from >> root;
			return root;
		}

	} // namespace

	void ImportST(const std::string_view jsonPath, SymbolTable& table) {
		JsonArchive from{jsonPath.data()};
		ImportStructures(GetArchiveRoot(from), table);

		assert(from.good());
		assert(from.is_open());
	}

	void ImportSources(const std::string_view jsonPath, Sources& srcs) {
		assert(srcs.empty());

		JsonArchive from{jsonPath.data()};
		const auto srcVal =  Get(GetArchiveRoot(from), "sources");
		assert(srcVal.isArray());

		srcs.reserve(std::distance(std::begin(srcVal), std::end(srcVal)));
		std::transform(std::begin(srcVal), std::end(srcVal), std::back_inserter(srcs), [](const auto& val) {
			assert(val.isString());
			return val.asString();
		});

		assert(from.good());
		assert(from.is_open());
	}

	Sources DropParsedFiles(const Sources& sources, const Sources& cached) {
		assert(sources.size() >= cached.size());
		assert((std::begin(sources) + (sources.size() - 1)) <= std::end(sources));

		return cached.empty() ? sources : Sources{std::begin(sources) + (cached.size() - 1), std::end(sources)};
	}

} // incremental