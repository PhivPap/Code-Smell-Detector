// Contains incremetnal integration tests
// Soultatos Stefanos 2022

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "DependenciesMining.h"
#include "SymbolTable.h"
#include "Utility.h"
#include "ImportST.h"

#include <cassert>
#include <filesystem>


namespace {

	using namespace dependenciesMining;
	using namespace tests::utility;
	using namespace incremental;

	void ExportST(const SymbolTable& table, const std::string_view jsonPath) {
		assert(!std::filesystem::exists(jsonPath));

		Json::Value jsonST;
		table.AddJsonSymbolTable(jsonST["structures"]);
		std::ofstream jsonSTFile{jsonPath.data()};
		jsonSTFile << jsonST;

		assert(std::filesystem::exists(jsonPath));
	}

	TEST(IncrementalTests, Importing_after_exporting_ST) {
		constexpr auto tmp = "out.json";
		const auto cmp_db = RESOLVE_PATH("compile_commands.json");
		std::vector<std::string> srcs;
		std::vector<std::string> headers;
		CreateClangTool(cmp_db.c_str(), srcs, headers, "", "");
		ExportST(structuresTable, tmp);

		SymbolTable imported;
		ImportStashedST(tmp, imported);

		EXPECT_TRUE(AreEqual(imported, structuresTable));

		std::remove(tmp);
	}


} // namespace


