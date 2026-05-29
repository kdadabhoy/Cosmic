#include "utils/DataExport.h"
#include "core/Log.h"
#include <fstream>
#include <filesystem>
#include <limits>

namespace Cosmic
{
	// -----------------------------------------------------------------------
	// Internal helper
	// -----------------------------------------------------------------------

	static bool EnsureParentDirectory(const std::string& filepath)
	{
		std::filesystem::path parent = std::filesystem::path(filepath).parent_path();
		if (!parent.empty() && !std::filesystem::exists(parent))
		{
			std::error_code ec;
			std::filesystem::create_directories(parent, ec);
			if (ec)
			{
				CS_CORE_ERROR("DataExport: could not create directory '{0}': {1}", parent.string(), ec.message());
				return false;
			}
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// WriteCSV
	// -----------------------------------------------------------------------

	bool DataExport::WriteCSV(
		const std::string& filepath,
		const std::vector<std::string>& headers,
		const std::vector<std::vector<double>>& columns)
	{
		if (headers.size() != columns.size())
		{
			CS_CORE_ERROR("DataExport::WriteCSV: header count ({0}) does not match column count ({1}).",
				headers.size(), columns.size());
			return false;
		}

		if (!columns.empty())
		{
			size_t rowCount = columns[0].size();
			for (size_t i = 1; i < columns.size(); ++i)
			{
				if (columns[i].size() != rowCount)
				{
					CS_CORE_ERROR("DataExport::WriteCSV: column {0} has {1} rows but column 0 has {2}.",
						i, columns[i].size(), rowCount);
					return false;
				}
			}
		}

		if (!EnsureParentDirectory(filepath))
			return false;

		std::ofstream file(filepath);
		if (!file.is_open())
		{
			CS_CORE_ERROR("DataExport::WriteCSV: could not open '{0}' for writing.", filepath);
			return false;
		}

		file.precision(std::numeric_limits<double>::max_digits10);

		// Header row
		for (size_t i = 0; i < headers.size(); ++i)
		{
			file << headers[i];
			if (i + 1 < headers.size()) file << ',';
		}
		file << '\n';

		// Data rows
		if (!columns.empty())
		{
			size_t rowCount = columns[0].size();
			for (size_t row = 0; row < rowCount; ++row)
			{
				for (size_t col = 0; col < columns.size(); ++col)
				{
					file << columns[col][row];
					if (col + 1 < columns.size()) file << ',';
				}
				file << '\n';
			}

			CS_CORE_INFO("DataExport::WriteCSV: wrote {0} rows to '{1}'.", rowCount, filepath);
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// AppendRow
	// -----------------------------------------------------------------------

	bool DataExport::AppendRow(
		const std::string& filepath,
		const std::vector<double>& values)
	{
		if (!EnsureParentDirectory(filepath))
			return false;

		std::ofstream file(filepath, std::ios::app);
		if (!file.is_open())
		{
			CS_CORE_ERROR("DataExport::AppendRow: could not open '{0}' for appending.", filepath);
			return false;
		}

		file.precision(std::numeric_limits<double>::max_digits10);

		for (size_t i = 0; i < values.size(); ++i)
		{
			file << values[i];
			if (i + 1 < values.size()) file << ',';
		}
		file << '\n';

		return true;
	}

	// -----------------------------------------------------------------------
	// WriteCircularBuffer
	// -----------------------------------------------------------------------

	bool DataExport::WriteCircularBuffer(
		const std::string& filepath,
		const std::vector<std::string>& headers,
		const std::vector<const float*>& buffers,
		int count,
		int offset,
		int capacity)
	{
		if (headers.size() != buffers.size())
		{
			CS_CORE_ERROR("DataExport::WriteCircularBuffer: header count ({0}) does not match buffer count ({1}).",
				headers.size(), buffers.size());
			return false;
		}

		if (count > capacity)
		{
			CS_CORE_ERROR("DataExport::WriteCircularBuffer: count ({0}) exceeds capacity ({1}).", count, capacity);
			return false;
		}

		if (!EnsureParentDirectory(filepath))
			return false;

		std::ofstream file(filepath);
		if (!file.is_open())
		{
			CS_CORE_ERROR("DataExport::WriteCircularBuffer: could not open '{0}' for writing.", filepath);
			return false;
		}

		file.precision(std::numeric_limits<double>::max_digits10);

		// Header row
		for (size_t i = 0; i < headers.size(); ++i)
		{
			file << headers[i];
			if (i + 1 < headers.size()) file << ',';
		}
		file << '\n';

		// Data rows in chronological order, unwrapping the circular index
		for (int i = 0; i < count; ++i)
		{
			int idx = (offset + i) % capacity;
			for (size_t col = 0; col < buffers.size(); ++col)
			{
				file << buffers[col][idx];
				if (col + 1 < buffers.size()) file << ',';
			}
			file << '\n';
		}

		CS_CORE_INFO("DataExport::WriteCircularBuffer: wrote {0} rows to '{1}'.", count, filepath);
		return true;
	}
}
