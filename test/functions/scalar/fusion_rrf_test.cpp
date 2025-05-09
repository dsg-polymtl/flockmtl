#include <flockmtl/functions/scalar/fusion_rrf.hpp>
#include <gtest/gtest.h>

using namespace duckdb;

TEST(FusionRRF, With2Doubles) {
    // Define the column types (2 BIGINT columns)
    const duckdb::vector<duckdb::LogicalType> types = {duckdb::LogicalType::BIGINT, duckdb::LogicalType::BIGINT};

    // Create a DataChunk and initialize it with the default allocator
    duckdb::DataChunk chunk;
    auto& allocator = duckdb::Allocator::DefaultAllocator();
    // Initialize with capacity 1 (one row)
    chunk.Initialize(allocator, types, 1);

    // Set the cardinality (number of rows) to 1
    chunk.SetCardinality(1);

    // Populate the DataChunk with test data
    chunk.SetValue(0, 0, 1.0);
    chunk.SetValue(1, 0, 1.0);

    // Call FusionRRF with the prepared DataChunk
    const std::vector<double> result = flockmtl::FusionRRF::Operation(chunk);

    // Verify the result
    ASSERT_EQ(result.size(), 1);
    ASSERT_EQ(result[0], 1.0 / 61 + 1.0 / 61);
}

TEST(FusionRRF, With2Columns) {
    // Define the column types (2 DOUBLE columns)
    const duckdb::vector<duckdb::LogicalType> types = {duckdb::LogicalType::BIGINT, duckdb::LogicalType::BIGINT};

    // Create a DataChunk and initialize it with the default allocator
    duckdb::DataChunk chunk;
    auto& allocator = duckdb::Allocator::DefaultAllocator();
    // Initialize with capacity 5 (five rows)
    chunk.Initialize(allocator, types, 5);

    // Set the cardinality (number of rows) to 5
    chunk.SetCardinality(5);

    // Data we will use to populate the DataChunk
    constexpr std::array<int32_t, 5> bm25_scores = {1, 2, 3, 4, 5};
    constexpr std::array<int32_t, 5> vs_scores = {4, 2, 1, 5, 3};

    // Populate the DataChunk with test data
    for (size_t i = 0; i < bm25_scores.size(); ++i) {
        chunk.SetValue(0, i, bm25_scores[i]);
        chunk.SetValue(1, i, vs_scores[i]);
    }

    // Call FusionRRF with the prepared DataChunk
    const std::vector<double> result = flockmtl::FusionRRF::Operation(chunk);

    // Verify the result
    constexpr std::array<double, 5> expected_results = {(1.0 / 61 + 1.0 / 64), (2.0 / 62), (1.0 / 63 + 1.0 / 61),
                                                        (1.0 / 64 + 1.0 / 65), (1.0 / 65 + 1.0 / 63)};
    ASSERT_EQ(result.size(), expected_results.size());
    for (size_t i = 0; i < expected_results.size(); ++i) {
        ASSERT_EQ(result[i], expected_results[i]);
    }
}

TEST(FusionRRF, With3Columns) {
    // Define the column types (2 DOUBLE columns)
    const duckdb::vector<duckdb::LogicalType> types = {duckdb::LogicalType::BIGINT, duckdb::LogicalType::BIGINT,
                                                       duckdb::LogicalType::BIGINT};

    // Create a DataChunk and initialize it with the default allocator
    duckdb::DataChunk chunk;
    auto& allocator = duckdb::Allocator::DefaultAllocator();
    // Initialize with capacity 5 (five rows)
    chunk.Initialize(allocator, types, 5);

    // Set the cardinality (number of rows) to 5
    chunk.SetCardinality(5);

    // Data we will use to populate the DataChunk
    constexpr std::array<int32_t, 5> bm25_scores = {1, 2, 3, 4, 5};
    constexpr std::array<int32_t, 5> vs_scores = {4, 2, 1, 5, 3};
    constexpr std::array<int32_t, 5> random_scores = {5, 4, 3, 2, 1};

    // Populate the DataChunk with test data
    for (size_t i = 0; i < bm25_scores.size(); ++i) {
        chunk.SetValue(0, i, bm25_scores[i]);
        chunk.SetValue(1, i, vs_scores[i]);
        chunk.SetValue(2, i, random_scores[i]);
    }

    // Call FusionRRF with the prepared DataChunk
    const std::vector<double> result = flockmtl::FusionRRF::Operation(chunk);

    // Verify the result
    std::vector<double> expected_results(5);
    for (int i = 0; i < 5; ++i) {
        expected_results[i] =
                1.0 / (60.0 + bm25_scores[i]) + 1.0 / (60.0 + vs_scores[i]) + 1.0 / (60.0 + random_scores[i]);
    }
    ASSERT_EQ(result.size(), expected_results.size());
    for (size_t i = 0; i < expected_results.size(); ++i) {
        ASSERT_EQ(result[i], expected_results[i]);
    }
}

// Entire columns of first place ranking essentially mean the scoring source did not rank anything and should be
// ignored.
TEST(FusionRRF, WithEntireColumnOfFirstPlaceRanking) {
    // Define the column types (2 DOUBLE columns)
    const duckdb::vector<duckdb::LogicalType> types = {duckdb::LogicalType::BIGINT, duckdb::LogicalType::BIGINT};

    // Create a DataChunk and initialize it with the default allocator
    duckdb::DataChunk chunk;
    auto& allocator = duckdb::Allocator::DefaultAllocator();
    // Initialize with capacity 5 (five rows)
    chunk.Initialize(allocator, types, 5);

    // Set the cardinality (number of rows) to 5
    chunk.SetCardinality(5);

    // Data we will use to populate the DataChunk
    constexpr std::array<int32_t, 5> vs_scores = {3, 1, 5, 2, 4};

    // Populate the DataChunk with test data
    for (size_t i = 0; i < vs_scores.size(); ++i) {
        chunk.SetValue(0, i, 1);
        chunk.SetValue(1, i, vs_scores[i]);
    }

    // Call FusionRRF with the prepared DataChunk
    const std::vector<double> result = flockmtl::FusionRRF::Operation(chunk);

    // Verify the result
    constexpr std::array<double, 5> expected_results = {1.0 / 63, 1.0 / 61, 1.0 / 65, 1.0 / 62, 1.0 / 64};
    ASSERT_EQ(result.size(), expected_results.size());
    for (size_t i = 0; i < expected_results.size(); ++i) {
        ASSERT_EQ(result[i], expected_results[i]);
    }
}