#include <algorithm>
#include <cmath>
#include <functional>
#include <inja/inja.hpp>
#include <iostream>
#include <large_flock/common.hpp>
#include <large_flock/core/functions/scalar.hpp>
#include <large_flock/core/model_manager/model_manager.hpp>
#include <large_flock/core/model_manager/openai.hpp>
#include <large_flock/core/model_manager/tiktoken.hpp>
#include <large_flock/core/parser/llm_response.hpp>
#include <large_flock/core/parser/scalar.hpp>
#include <large_flock_extension.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <templates/lf_filter_prompt_template.hpp>

namespace large_flock {
namespace core {

template <typename T>
std::string ToString2(const T &value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

nlohmann::json GetMaxLengthValues2(const std::vector<nlohmann::json> &params) {
    nlohmann::json attr_to_max_token_length;

    for (const auto &json_obj : params) {
        for (const auto &item : json_obj.items()) {
            auto key = item.key();
            auto value_str = ToString2(item.value());
            int length = value_str.length();

            if (attr_to_max_token_length.contains(key)) {
                auto current_max_value_str = ToString2(attr_to_max_token_length[key]);
                if (current_max_value_str.length() < length) {
                    attr_to_max_token_length[key] = item.value();
                }
            } else {
                attr_to_max_token_length[key] = item.value();
            }
        }
    }

    return attr_to_max_token_length;
}

std::string combine_values2(const nlohmann::json &json_obj) {
    std::string combined;
    for (const auto &item : json_obj.items()) {
        combined += ToString2(item.value()) + " ";
    }

    if (!combined.empty()) {
        combined.pop_back();
    }
    return combined;
}

inline std::vector<std::string> ConstructPrompts2(std::vector<nlohmann::json> &unique_rows, Connection &con,
                                                  std::string prompt_name, int model_max_tokens = 4096) {
    inja::Environment env;

    auto query_result = con.Query(
        "SELECT prompt FROM lf_config.LARGE_FLOCK_PROMPT_INTERNAL_TABLE WHERE prompt_name = '" + prompt_name + "'");

    if (query_result->RowCount() == 0) {
        throw std::runtime_error("Prompt not found");
    }

    auto template_str = query_result->GetValue(0, 0).ToString();
    auto row_tokens = Tiktoken::GetNumTokens(template_str);
    auto max_length_values = GetMaxLengthValues2(unique_rows);
    auto combined_values = combine_values2(max_length_values);
    row_tokens += Tiktoken::GetNumTokens(combined_values);

    std::vector<std::string> prompts;

    if (row_tokens > model_max_tokens) {
        throw std::runtime_error("The total number of tokens in the prompt exceeds the model's maximum token limit");
    } else {
        auto template_tokens = Tiktoken::GetNumTokens(lf_filter_prompt_template);
        auto max_tokens_for_rows = model_max_tokens - template_tokens;
        auto max_chunk_size = max_tokens_for_rows / row_tokens;
        auto chunk_size = std::min(max_chunk_size, static_cast<int>(unique_rows.size()));
        auto num_chunks = static_cast<int>(std::ceil(static_cast<double>(unique_rows.size()) / chunk_size));

        for (int i = 0; i < num_chunks; ++i) {
            nlohmann::json data;
            data["prompts"] = template_str;

            for (int j = 0; j < chunk_size; ++j) {
                data["rows"].push_back(unique_rows[i + j]);
            }

            auto prompt = env.render(lf_filter_prompt_template, data);
            prompts.push_back(prompt);
        }
    }

    return prompts;
}

inline std::tuple<std::vector<int>, std::vector<nlohmann::json>> PrepareCache2(DataChunk &args) {
    auto inputs = CoreScalarParsers::Struct2Json(args.data[2], args.size());

    std::vector<int> result_indexes;
    std::vector<nlohmann::json> unique_rows;

    // Process each JSON object
    int unique_index = 0;
    for (const auto &row : inputs) {
        auto it = std::find(unique_rows.begin(), unique_rows.end(), row);
        if (it != unique_rows.end()) {
            auto row_index = std::distance(unique_rows.begin(), it);
            result_indexes.push_back(row_index);
        } else {
            unique_rows.push_back(row);
            result_indexes.push_back(unique_index++);
        }
    }

    return {result_indexes, unique_rows};
}

static void LfFilterScalarFunction2(DataChunk &args, ExpressionState &state, Vector &result) {
    Connection con(*state.GetContext().db);
    CoreScalarParsers::LfMapScalarParser(args);

    auto model = args.data[1].GetValue(0).ToString();
    auto query_result = con.Query(
        "SELECT model, max_tokens FROM lf_config.LARGE_FLOCK_MODEL_INTERNAL_TABLE WHERE model_name = '" + model + "'");

    if (query_result->RowCount() == 0) {
        throw std::runtime_error("Model not found");
    }

    auto model_name = query_result->GetValue(0, 0).ToString();
    auto model_max_tokens = query_result->GetValue(1, 0).GetValue<int32_t>();

    auto [results_indexes, unique_rows] = PrepareCache2(args);

    auto prompts = ConstructPrompts2(unique_rows, con, args.data[0].GetValue(0).ToString(), model_max_tokens);

    nlohmann::json settings;
    if (args.ColumnCount() == 4) {
        settings = CoreScalarParsers::Struct2Json(args.data[3], 1)[0];
    }

    auto results_cache = nlohmann::json::array();
    for (const auto &prompt : prompts) {
        // Call ModelManager::CallComplete and get the rows
        auto result = ModelManager::CallComplete(prompt, model_name, settings);

        // Check if the result contains the 'rows' field and push it to the main 'rows'
        if (result.contains("rows")) {
            for (const auto &row : result["rows"]) {
                results_cache.push_back(row);
            }
        }
    }

    auto index = 0;
    Vector vec(LogicalType::VARCHAR, args.size());
    UnaryExecutor::Execute<string_t, string_t>(vec, result, args.size(), [&](string_t _) {
        return StringVector::AddString(result, results_cache[results_indexes[index++]].dump());
    });
}

void CoreScalarFunctions::RegisterLfFilterScalarFunction(DatabaseInstance &db) {
    ExtensionUtil::RegisterFunction(db, ScalarFunction("lf_filter", {}, LogicalType::VARCHAR, LfFilterScalarFunction2,
                                                       nullptr, nullptr, nullptr, nullptr, LogicalType::ANY));
}

} // namespace core
} // namespace large_flock
