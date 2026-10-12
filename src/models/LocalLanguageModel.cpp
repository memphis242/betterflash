#include "LocalLanguageModel.h"

#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <chat.h>
#include <ggml-backend.h>
#include <llama.h>
#include <sampling.h>
#include <algorithm>
#include <array>
#include <mutex>

namespace {
constexpr uint32_t compactContext = 4096;
constexpr uint32_t desktopContext = 8192;
static_assert(compactContext >= 2048 && desktopContext <= 8192);
qint64 memoryKiB(const QByteArray &field) {
    QFile information(QStringLiteral("/proc/meminfo"));
    if (!information.open(QIODevice::ReadOnly)) return 0;
    for (const QByteArray &line : information.readAll().split('\n')) {
        if (line.startsWith(field + ':')) {
            bool okay = false;
            const qint64 value = line.mid(field.size() + 1).simplified().split(' ').first().toLongLong(&okay);
            return okay && value > 0 ? value : 0;
        }
    }
    return 0;
}
struct InferenceBudget {
    const std::shared_ptr<std::atomic_bool> canceled;
    const QDeadlineTimer deadline{120000};
    [[nodiscard]] bool stopped() const { return canceled->load() || deadline.hasExpired(); }
};
bool abortInference(void *opaque) { return static_cast<const struct InferenceBudget *>(opaque)->stopped(); }
bool continueLoading(float, void *opaque) { return !abortInference(opaque); }
}

struct LocalLanguageSpec LocalLanguageModel::spec(const QString &profile) {
    if (profile == QStringLiteral("gemma")) {
        return {QStringLiteral("gemma/gemma-4-E2B-it-Q4_K_M.gguf"), QStringLiteral("Gemma 4 E2B / Q4_K_M / 3.1 GB download"),
            {QUrl(QStringLiteral("https://huggingface.co/unsloth/gemma-4-E2B-it-GGUF/resolve/0314792d7f1f7e229411f620751375812bb9faf2/gemma-4-E2B-it-Q4_K_M.gguf")),
             QStringLiteral("gemma/gemma-4-E2B-it-Q4_K_M.gguf"), 3106738272LL,
             QByteArrayLiteral("740185b21d22ceb83a11c3aa62ad5842ef32c70f6096d756bbee85a1e4ec34b8"), {}, {}}};
    }
    return {QStringLiteral("lfm/LFM2.5-350M-Q4_K_M.gguf"), QStringLiteral("LFM 2.5 350M / Q4_K_M / 229 MB download"),
        {QUrl(QStringLiteral("https://huggingface.co/LiquidAI/LFM2.5-350M-GGUF/resolve/657e078c94084481950a2d555a941481f715536b/LFM2.5-350M-Q4_K_M.gguf")),
         QStringLiteral("lfm/LFM2.5-350M-Q4_K_M.gguf"), 229312224,
         QByteArrayLiteral("7e6f72643caafc9a68256686638c4d7916f2cec76d1df478d4c3ddcd95a6aed4"), {}, {}}};
}
QString LocalLanguageModel::automaticProfile() {
#ifdef Q_OS_ANDROID
    return QStringLiteral("lfm");
#else
    return memoryKiB(QByteArrayLiteral("MemTotal")) >= 12LL * 1024 * 1024 ? QStringLiteral("gemma") : QStringLiteral("lfm");
#endif
}
std::expected<struct LocalLanguageResult, QString> LocalLanguageModel::generate(
    const QString &path, const QString &acceleration, const QString &profile,
    const QJsonObject &payload, const std::shared_ptr<std::atomic_bool> &canceled) {
    Q_ASSERT(canceled);
    try {
        struct InferenceBudget budget{canceled};
        if (budget.stopped()) return std::unexpected(QStringLiteral("LLM_CANCELED: Request canceled."));
        static std::once_flag initialization;
        std::call_once(initialization, [] {
            llama_log_set([](ggml_log_level, const char *, void *) {}, nullptr);
            llama_backend_init();
            ggml_backend_load_all();
        });
        const qint64 available = memoryKiB(QByteArrayLiteral("MemAvailable"));
        const qint64 minimum = profile == QStringLiteral("gemma") ? 4LL * 1024 * 1024 : 600LL * 1024;
        if (available > 0 && available < minimum)
            return std::unexpected(QStringLiteral("LLM_MEMORY: Insufficient free memory for this profile. Select LFM 2.5 350M or close another application."));
        struct llama_model_params modelParameters = llama_model_default_params();
        modelParameters.n_gpu_layers = 0;
        modelParameters.progress_callback = continueLoading;
        modelParameters.progress_callback_user_data = &budget;
        QString accelerator = QStringLiteral("CPU");
        std::array<ggml_backend_dev_t, 2> devices{};
        if (acceleration == QStringLiteral("automatic")) {
            const size_t required = static_cast<size_t>(QFileInfo(path).size()) + 512ULL * 1024 * 1024;
            for (size_t index = 0; index < ggml_backend_dev_count(); ++index) {
                const ggml_backend_dev_t device = ggml_backend_dev_get(index);
                if (ggml_backend_dev_type(device) != GGML_BACKEND_DEVICE_TYPE_GPU) continue;
                size_t free = 0, total = 0;
                ggml_backend_dev_memory(device, &free, &total);
                if (free < required) continue;
                devices[0] = device;
                modelParameters.devices = devices.data();
                modelParameters.n_gpu_layers = -1;
                modelParameters.split_mode = LLAMA_SPLIT_MODE_NONE;
                accelerator = QString::fromUtf8(ggml_backend_dev_description(device));
                break;
            }
        }
        const QByteArray modelPath = QFile::encodeName(path);
        std::unique_ptr<struct llama_model, decltype(&llama_model_free)> model(
            llama_model_load_from_file(modelPath.constData(), modelParameters), llama_model_free);
        if (!model && !budget.stopped() && modelParameters.n_gpu_layers != 0) {
            modelParameters.n_gpu_layers = 0; modelParameters.devices = nullptr;
            model.reset(llama_model_load_from_file(modelPath.constData(), modelParameters));
            accelerator = QStringLiteral("CPU (GPU allocation unavailable)");
        }
        if (budget.stopped()) return std::unexpected(QStringLiteral("LLM_CANCELED: Request canceled or exceeded its two-minute limit."));
        if (!model) return std::unexpected(QStringLiteral("LLM_LOAD: Cannot load the local model. Download it again or select the compact profile."));
        const struct llama_vocab *const vocabulary = llama_model_get_vocab(model.get());
        const common_chat_templates_ptr templates = common_chat_templates_init(model.get(), "");
        struct common_chat_templates_inputs inputs{};
        inputs.enable_thinking = false;
        const QJsonArray messages = payload.value(QStringLiteral("messages")).toArray();
        for (const QJsonValue &value : messages) {
            const QJsonObject message = value.toObject();
            struct common_chat_msg item{};
            item.role = message.value(QStringLiteral("role")).toString().toStdString();
            item.content = message.value(QStringLiteral("content")).toString().toStdString();
            inputs.messages.push_back(std::move(item));
        }
        if (payload.value(QStringLiteral("response_format")).toObject().value(QStringLiteral("type")).toString() == QStringLiteral("json_object"))
            inputs.json_schema = R"({"type":"object"})";
        common_chat_session session(templates.get(), vocabulary, inputs);
        const std::vector<llama_token> prompt = common_tokenize(vocabulary, session.prompt(), true, true);
        const uint32_t capacity = profile == QStringLiteral("gemma") ? desktopContext : compactContext;
        const int maximum = std::clamp(payload.value(QStringLiteral("max_tokens")).toInt(600), 1, 3000);
        if (prompt.empty() || prompt.size() + static_cast<size_t>(maximum) > capacity)
            return std::unexpected(QStringLiteral("LLM_CONTEXT: This request exceeds the local model context. Use fewer or shorter cards, or select a provider with a larger context."));
        struct llama_context_params contextParameters = llama_context_default_params();
        contextParameters.n_ctx = capacity;
        contextParameters.n_batch = 256;
        contextParameters.n_ubatch = 128;
        contextParameters.n_threads = std::clamp(QThread::idealThreadCount() / 2, 1, 8);
        contextParameters.n_threads_batch = contextParameters.n_threads;
        contextParameters.abort_callback = abortInference;
        contextParameters.abort_callback_data = &budget;
        std::unique_ptr<struct llama_context, decltype(&llama_free)> context(
            llama_init_from_model(model.get(), contextParameters), llama_free);
        if (!context) return std::unexpected(QStringLiteral("LLM_CONTEXT_MEMORY: Cannot allocate the local model context. Select the compact profile or CPU execution."));
        struct common_params_sampling sampling{};
        sampling.temp = static_cast<float>(payload.value(QStringLiteral("temperature")).toDouble(0.2));
        sampling.seed = 0;
        session.apply_sampling(sampling);
        std::unique_ptr<struct common_sampler, decltype(&common_sampler_free)> sampler(
            common_sampler_init(model.get(), sampling), common_sampler_free);
        if (!sampler) return std::unexpected(QStringLiteral("LLM_SAMPLER: Cannot initialize model output constraints. Choose another model profile."));
        for (size_t offset = 0; offset < prompt.size();) {
            if (budget.stopped()) return std::unexpected(QStringLiteral("LLM_CANCELED: Request canceled or exceeded its two-minute limit."));
            const int32_t count = static_cast<int32_t>(std::min<size_t>(256, prompt.size() - offset));
            const struct llama_batch batch = llama_batch_get_one(const_cast<llama_token *>(prompt.data() + offset), count);
            if (llama_decode(context.get(), batch) != 0)
                return std::unexpected(QStringLiteral("LLM_DECODE: Cannot process the request. Select CPU execution or a smaller model."));
            offset += static_cast<size_t>(count);
        }
        bool ended = false;
        for (int index = 0; index < maximum; ++index) {
            if (budget.stopped()) return std::unexpected(QStringLiteral("LLM_CANCELED: Request canceled or exceeded its two-minute limit."));
            llama_token token = common_sampler_sample(sampler.get(), context.get(), -1);
            common_sampler_accept(sampler.get(), token, true);
            if (llama_vocab_is_eog(vocabulary, token)) { ended = true; break; }
            const std::string piece = common_token_to_piece(vocabulary, token, true);
            struct common_chat_input chunk{}; chunk.append(piece, token);
            session.feed(chunk);
            const struct llama_batch batch = llama_batch_get_one(&token, 1);
            if (llama_decode(context.get(), batch) != 0)
                return std::unexpected(QStringLiteral("LLM_DECODE: Model response generation failed. Select CPU execution or a smaller model."));
        }
        const QString result = QString::fromStdString(session.finish().content).trimmed();
        if (!ended) return std::unexpected(QStringLiteral("LLM_OUTPUT_LIMIT: The model response exceeded its limit. Try fewer cards or another model profile."));
        if (result.isEmpty() || result.size() > 110000)
            return std::unexpected(QStringLiteral("LLM_RESPONSE: The local model returned no usable answer. Try another profile."));
        return LocalLanguageResult{result, accelerator};
    } catch (const std::exception &) {
        return std::unexpected(QStringLiteral("LLM_LOCAL_RUNTIME: Local generation failed. Retry with CPU execution or download another model profile."));
    }
}
