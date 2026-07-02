#pragma once

// DataExport.h
// Last Modified 5/26/2026

/**
 * General Description:
 *
 * DataExport is a static utility class for flushing simulation data captured
 * inside engine layers out to disk in CSV format. It mirrors the design of
 * FileSystem — a thin, stateless service with no GPU dependency, safe to call
 * from any layer hook.
 *
 * Three export modes are supported:
 *
 * 1. WriteCSV — bulk write of pre-collected column data with a header row.
 *    Use this when you have already gathered your data into parallel vectors
 *    (e.g. post-run analysis, one-shot data dumps).
 *
 * 2. AppendRow — write a single row to an already-open (or newly created) file.
 *    Use this for streaming data out frame-by-frame or step-by-step during a
 *    live simulation. The file is opened, written, and closed on every call;
 *    this is safe but not optimal for very high frequency logging. For
 *    sub-millisecond logging, buffer rows in a vector and call WriteCSV at
 *    the end of the run instead.
 *
 * 3. WriteCircularBuffer — handles the wrap-around indexing of ImPlot-style
 *    circular float buffers directly, outputting rows in chronological order
 *    without requiring the caller to copy or reorder data first.
 *
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. static bool WriteCSV(
 *        const std::string& filepath,
 *        const std::vector<std::string>& headers,
 *        const std::vector<std::vector<double>>& columns)
 *    Pre:  All column vectors must have the same length. headers.size() must
 *          equal columns.size(). The directory containing filepath must exist
 *          or be creatable.
 *    Post: A CSV file is written at filepath. Row 0 is the comma-separated
 *          header. Subsequent rows contain one value per column, in the order
 *          provided. Returns true on success, false if the file could not be
 *          opened or if a size mismatch is detected.
 *
 * 2. static bool AppendRow(
 *        const std::string& filepath,
 *        const std::vector<double>& values)
 *    Pre:  filepath must be a valid writable path.
 *    Post: Opens the file in append mode, writes the values as a single
 *          comma-separated row, and closes the file. If the file did not
 *          previously exist it is created. Returns true on success.
 *
 * 3. static bool WriteCircularBuffer(
 *        const std::string& filepath,
 *        const std::vector<std::string>& headers,
 *        const std::vector<const float*>& buffers,
 *        int count, int offset, int capacity)
 *    Pre:  buffers.size() must equal headers.size(). Every pointer in buffers
 *          must point to an array of at least `capacity` floats. count <= capacity.
 *    Post: Iterates the circular buffers in chronological order (from offset
 *          to offset + count, modulo capacity), writing one row per sample.
 *          Returns true on success.
 */

#include "core/Core.h"
#include <string>
#include <vector>

namespace Cosmic
{
	class COSMIC_API DataExport
	{
	public:
		// -----------------------------------------------------------------------
		// Bulk write — all data collected in memory first
		// -----------------------------------------------------------------------

		/**
		 * @brief Write a multi-column CSV file from parallel column vectors.
		 *
		 * @param filepath  Output path. Parent directory is created if absent.
		 * @param headers   Column names; size must equal columns.size().
		 * @param columns   Parallel vectors of sample values; all must have the
		 *                  same length.
		 * @return True on success.
		 */
		static bool WriteCSV(
			const std::string& filepath,
			const std::vector<std::string>& headers,
			const std::vector<std::vector<double>>& columns
		);

		// -----------------------------------------------------------------------
		// Streaming write — one row at a time during a live simulation
		// -----------------------------------------------------------------------

		/**
		 * @brief Append a single data row to a CSV file.
		 *
		 * The file is opened in append mode on each call. If the file does not
		 * exist it is created. No header row is written by this method — call
		 * WriteCSV with an empty columns vector first, or write the header
		 * manually with a first AppendRow call containing your header strings
		 * cast to double (not recommended — use WriteCSV for that case).
		 *
		 * For high-frequency streaming consider buffering rows in a
		 * std::vector<std::vector<double>> and calling WriteCSV at run end.
		 *
		 * @param filepath  Output path. Parent directory is created if absent.
		 * @param values    One value per column for this row.
		 * @return True on success.
		 */
		static bool AppendRow(
			const std::string& filepath,
			const std::vector<double>& values
		);

		// -----------------------------------------------------------------------
		// Circular buffer write — ImPlot-compatible layout
		// -----------------------------------------------------------------------

		/**
		 * @brief Write data from one or more ImPlot-style circular float buffers.
		 *
		 * Buffers are iterated in chronological order using the standard
		 * (offset + i) % capacity index pattern. This correctly handles the
		 * wrap-around without requiring the caller to copy or linearise data.
		 *
		 * @param filepath   Output path. Parent directory is created if absent.
		 * @param headers    Column names; size must equal buffers.size().
		 * @param buffers    Pointers to circular float arrays, one per column.
		 *                   Each array must contain at least `capacity` elements.
		 * @param count      Number of valid samples currently in the buffers
		 *                   (i.e. the value you track as m_PlotCount).
		 * @param offset     Index of the oldest sample (i.e. m_PlotOffset).
		 * @param capacity   Total allocated size of each buffer array
		 *                   (i.e. k_PlotBufferSize).
		 * @return True on success.
		 */
		static bool WriteCircularBuffer(
			const std::string& filepath,
			const std::vector<std::string>& headers,
			const std::vector<const float*>& buffers,
			int count,
			int offset,
			int capacity
		);

		// -----------------------------------------------------------------------
		// Load — WriteCSV's read counterpart (E13: lookup tables from CSV data)
		// -----------------------------------------------------------------------

		/**
		 * @brief Read a numeric CSV file into parallel column vectors.
		 *
		 * The transpose of WriteCSV. If the first row contains any non-numeric
		 * cell it is treated as the header row (returned via outHeaders when
		 * non-null, otherwise skipped). Ragged rows and rows with non-numeric
		 * cells after the header are rejected with a logged error.
		 *
		 * @param filepath    Input path (goes through no VFS resolution — pass a
		 *                    resolved or relative disk path, or resolve with
		 *                    FileSystem::Resolve first).
		 * @param outColumns  Receives one vector per column, all equal length.
		 * @param outHeaders  Optional; receives the header row if one existed
		 *                    (empty when the file starts with data).
		 * @return True on success (at least one data row).
		 */
		static bool LoadCSV(
			const std::string& filepath,
			std::vector<std::vector<double>>& outColumns,
			std::vector<std::string>* outHeaders = nullptr
		);
	};
}