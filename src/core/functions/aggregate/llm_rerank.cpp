#include <nlohmann/json.hpp>
#include <inja/inja.hpp>
#include <flockmtl/core/functions/prompt_builder.hpp>
#include "flockmtl/core/module.hpp"
#include "templates/llm_rerank_prompt_template.hpp"
#include <flockmtl/common.hpp>
#include <flockmtl/core/functions/aggregate.hpp>
#include <flockmtl/core/model_manager/model_manager.hpp>
#include <flockmtl_extension.hpp>
#include <flockmtl/core/model_manager/tiktoken.hpp>
#include <flockmtl/core/functions/aggregate/llm_agg.hpp>
#include "templates/llm_rerank_prompt_template.hpp"

namespace flockmtl {
namespace core {

class LlmReranker {
public:
    std::string model;
    int model_context_size;
    std::string user_prompt;
    std::string llm_reranking_template;
    int available_tokens;

    int CalculateFixedTokens() const {
        int num_tokens_meta_and_user_prompts = 0;
        num_tokens_meta_and_user_prompts += Tiktoken::GetNumTokens(user_prompt);
        num_tokens_meta_and_user_prompts += Tiktoken::GetNumTokens(llm_reranking_template);
        return num_tokens_meta_and_user_prompts;
    }

    LlmReranker(std::string& model, int model_context_size, std::string& user_prompt, std::string& llm_reranking_template)
    : model(model), model_context_size(model_context_size), user_prompt(user_prompt), llm_reranking_template(llm_reranking_template) {

        auto num_tokens_meta_and_user_prompts = CalculateFixedTokens();

        if (num_tokens_meta_and_user_prompts > model_context_size) {
            throw std::runtime_error("Fixed tokens exceed model context size");
        }

        available_tokens = model_context_size - num_tokens_meta_and_user_prompts;
    };

    nlohmann::json LlmRerank(const nlohmann::json& tuples) {
        inja::Environment env;
        nlohmann::json data;
        data["tuples"] = tuples;
        data["user_prompt"] = user_prompt;
        auto prompt = env.render(llm_reranking_template, data);

        nlohmann::json settings;
        auto response = ModelManager::CallComplete(prompt, model, settings);
        return response["ranking"];
    };

    // Sliding window re-ranking
    nlohmann::json SlidingWindowRerank(nlohmann::json& tuples) {
        int num_tuples = tuples.size();

        auto accumulated_rows_tokens = 0u;
        auto batch_size = 0u;
        auto window_tuples = nlohmann::json::array();
        auto start_index = num_tuples - 1;
        auto reranked_tuples = nlohmann::json::array();

        do {
            while (available_tokens - accumulated_rows_tokens > 0 && start_index >= 0) {
                auto num_tokens = Tiktoken::GetNumTokens(tuples[start_index].dump());
                if (accumulated_rows_tokens + num_tokens > available_tokens) {
                    break;
                }
                window_tuples.push_back(tuples[start_index]);
                accumulated_rows_tokens += num_tokens;
                batch_size++;
                start_index--;
            }

            auto ranked_indices = LlmRerank(window_tuples);

            auto half_batch = batch_size / 2;
            auto next_tuples = nlohmann::json::array();
            for (auto i = 0; i < batch_size; i++) {
                if (i < half_batch) {
                    next_tuples.push_back(window_tuples[i]);
                } else {
                    reranked_tuples.push_back(window_tuples[i]);
                }
            }

            window_tuples.clear();
            window_tuples = std::move(next_tuples);
            batch_size = half_batch;
            accumulated_rows_tokens = Tiktoken::GetNumTokens(window_tuples.dump());
        } while (start_index >= 0);

        reranked_tuples.insert(reranked_tuples.end(), window_tuples.begin(), window_tuples.end());

        nlohmann::json results;
        for (auto i = num_tuples - 1; i >= num_tuples/2; i--) {
            results.push_back(reranked_tuples[i]["content"]);
        }

        return results;
    }
};

void LlmAggOperation::RerankerFinalize(Vector &states, AggregateInputData &aggr_input_data, Vector &result, idx_t count,
                         idx_t offset) {
    auto states_vector = FlatVector::GetData<LlmAggState *>(states);

    for (idx_t i = 0; i < count; i++) {
        auto idx = i + offset;
        auto state_ptr = states_vector[idx];
        auto state = state_map[state_ptr];

        auto query_result = CoreModule::GetConnection().Query(
            "SELECT model, max_tokens FROM flockmtl_config.FLOCKMTL_MODEL_INTERNAL_TABLE WHERE model_name = '" +
            model_name + "'");

        if (query_result->RowCount() == 0) {
            throw std::runtime_error("Model not found");
        }

        auto model = query_result->GetValue(0, 0).ToString();
        auto model_context_size = query_result->GetValue(1, 0).GetValue<int>();

        query_result = CoreModule::GetConnection().Query("SELECT prompt FROM flockmtl_config.FLOCKMTL_PROMPT_INTERNAL_TABLE WHERE prompt_name = '" +
              prompt_name + "'");

        if (query_result->RowCount() == 0) {
            throw std::runtime_error("Prompt not found");
        }

        auto user_prompt = query_result->GetValue(0, 0).ToString();
        auto llm_rerank_prompt_template_str = std::string(llm_rerank_prompt_template);

        auto tuples_with_ids = nlohmann::json::array();
        for (auto i = 0; i < state->value.size(); i++) {
            auto tuple_with_id = nlohmann::json::object();
            tuple_with_id["id"] = i;
            tuple_with_id["content"] = state->value[i];
            tuples_with_ids.push_back(tuple_with_id);
        }

        LlmReranker llm_reranker(model, model_context_size, user_prompt, llm_rerank_prompt_template_str);
        auto reranked_tuples = llm_reranker.SlidingWindowRerank(tuples_with_ids);

        result.SetValue(idx, reranked_tuples.dump());
    }
}

void CoreAggregateFunctions::RegisterLlmRerankFunction(DatabaseInstance &db) {
    auto string_concat = AggregateFunction(
        "llm_rerank", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::ANY}, LogicalType::JSON(),
        AggregateFunction::StateSize<LlmAggState>, LlmAggOperation::Initialize, LlmAggOperation::Operation,
        LlmAggOperation::Combine, LlmAggOperation::RerankerFinalize, LlmAggOperation::SimpleUpdate);

    ExtensionUtil::RegisterFunction(db, string_concat);
}

} // namespace core
} // namespace flockmtl