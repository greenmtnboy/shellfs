#define DUCKDB_EXTENSION_MAIN

#include "shellfs_extension.hpp"
#include "shell_file_system.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "query_farm_telemetry.hpp"
namespace duckdb
{

	static void LoadInternal(ExtensionLoader &loader)
	{
		// Register a scalar function
		auto &instance = loader.GetDatabaseInstance();
		auto &fs = instance.GetFileSystem();

		fs.RegisterSubSystem(make_uniq<ShellFileSystem>());

		auto &config = DBConfig::GetConfig(instance);

		// When writing to a PIPE ignore the SIGPIPE error and consider that the write succeeded.
		config.AddExtensionOption("ignore_sigpipe", "Ignore SIGPIPE", LogicalType::BOOLEAN, Value(false));

		// Use legacy pipe close behavior (close on zero-byte read without checking feof).
		// This option exists to demonstrate the Windows bug where _popen() can return 0 bytes
		// temporarily when buffers aren't ready. Setting this to true reproduces the bug.
		config.AddExtensionOption("use_legacy_pipe_close",
		                          "Use legacy pipe close behavior (for testing Windows bug)",
		                          LogicalType::BOOLEAN, Value(false));

		QueryFarmSendTelemetry(loader, "shellfs", "2025123001");
	}

	void ShellfsExtension::Load(ExtensionLoader &loader)
	{
		LoadInternal(loader);
	}

	std::string ShellfsExtension::Name()
	{
		return "shellfs";
	}

} // namespace duckdb

extern "C"
{

	DUCKDB_CPP_EXTENSION_ENTRY(shellfs, loader)
	{
		duckdb::LoadInternal(loader);
	}
}
