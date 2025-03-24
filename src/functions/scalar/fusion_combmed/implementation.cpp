#include "flockmtl/functions/scalar/fusion_combmed.hpp"
#include "flockmtl/helper_functions/normalizer.hpp"

namespace flockmtl {

void FusionCombMED::ValidateArguments(duckdb::DataChunk& args) {
    for (int i = 0; i < static_cast<int>(args.ColumnCount()); i++) {
        if (args.data[i].GetType() != duckdb::LogicalType::DOUBLE) {
            throw std::runtime_error("fusion_combmed: argument must be a double");
        }
    }
}

// performs CombMED to merge lists based on a calculated score.
std::vector<std::string> FusionCombMED::Operation(duckdb::DataChunk& args) {
    FusionCombMED::ValidateArguments(args);
    int num_different_scores = static_cast<int>(args.ColumnCount());
    int num_entries = static_cast<int>(args.size());

    // the function is sometimes called with a singular entry, often when null values are present in a table
    // in these cases, we return -1 to say that a ranking is impossible/invalid
    if (num_entries == 1) {
        return std::vector<std::string>(1, "-1 (INVALID)");
    }
    if (num_entries == 0) {
        return std::vector<std::string>(1, "");
    }

    // we want to keep track of all scores for each entry (in a vector)
    std::vector<std::pair<int, std::vector<double>>> cumulative_scores(num_entries);
    for (int i = 0; i < num_entries; i++) {
        cumulative_scores[i].first = i;
    }

    // for each column (scoring system), we want a vector of individual input scores
    for (int i = 0; i < num_different_scores; i++) {
        // extract a single column's score values. Initializing this way ensures 0 for null values
        std::vector<double> extracted_scores(num_entries);
        for (int j = 0; j < num_entries; j++) {
            auto valueWrapper = args.data[i].GetValue(j);
            // null values are left as 0, treated as if the entry is not present in that scoring system's results
            if (!valueWrapper.IsNull()) {
                extracted_scores[j] = valueWrapper.GetValue<double>();
            } else {
                extracted_scores[j] = std::numeric_limits<double>::quiet_NaN();
            }
        }
        // If all entries have the same score, then this scoring system can be considered useless and should be ignored
        if (std::adjacent_find(extracted_scores.begin(), extracted_scores.end(), std::not_equal_to<>()) == extracted_scores.end()) {
            continue;
        }

        // we now normalize each scoring system independently, increasing hit counts appropriately
        extracted_scores = Normalizer::normalize(extracted_scores, NormalizationMethod::Max);

        // add this column's scores to the scores for this entry
        for (int k = 0; k < num_entries; k++) {
            if (!std::isnan(extracted_scores[k])) {
                cumulative_scores[k].second.push_back(extracted_scores[k]);
            }
        }
    }

    // we will now calculate the median score for each entry
    std::vector<std::pair<int, double>> median_scores(num_entries);
    for (int i = 0; i < num_entries; i++) {
        median_scores[i].first = cumulative_scores[i].first;
        median_scores[i].second = FusionCombMED::calculateMedian(cumulative_scores[i].second);
    }

    // sort the medians so we can obtain the rankings
    std::sort(median_scores.begin(), median_scores.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

    // return the resulting ranking of all documents
    std::vector<std::string> results(num_entries);
    for (int i = 0; i < num_entries; i++) {
        const int tmp_index = median_scores[i].first;
        // add 1 to rank because we want rankings to start at 1, not 0
        results[tmp_index] = std::to_string(i + 1) + " (" + std::to_string(median_scores[i].second) + ")";
    }

    return results;
}

void FusionCombMED::Execute(duckdb::DataChunk& args, duckdb::ExpressionState& state, duckdb::Vector& result) {
    auto results = FusionCombMED::Operation(args);

    auto index = 0;
    for (const auto& res : results) {
        result.SetValue(index++, duckdb::Value(res));
    }
}

double FusionCombMED::calculateMedian(const std::vector<double>& data) {
    if (data.empty()) {
        return 0.0;
    }

    // Create a copy to avoid modifying the original vector
    std::vector<double> sorted_data = data;
    const size_t size = sorted_data.size();
    const size_t mid = size / 2;

    // Sort the copy
    std::sort(sorted_data.begin(), sorted_data.end());

    if (size % 2 == 0) {
        // For even-sized vectors, average the two middle elements
        return (sorted_data[mid - 1] + sorted_data[mid]) / 2.0;
    } else {
        // For odd-sized vectors, return the middle element
        return sorted_data[mid];
    }
}

} // namespace flockmtl
