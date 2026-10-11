#include "LocalEmbedder.h"

#include <QFile>
#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr qsizetype maxTokens = 512;
constexpr auto queryPrefix = "Represent this sentence for searching relevant passages: ";

bool isWhitespace(const QChar character)
{
    return character == QChar::fromLatin1(' ') || character == QChar::fromLatin1('\t')
        || character == QChar::fromLatin1('\n') || character == QChar::fromLatin1('\r')
        || character.category() == QChar::Separator_Space;
}

bool isControl(const QChar character)
{
    if (character == QChar::fromLatin1('\t') || character == QChar::fromLatin1('\n')
        || character == QChar::fromLatin1('\r'))
        return false;
    return character.category() == QChar::Other_Control || character.category() == QChar::Other_Format;
}

bool isPunctuation(const QChar character)
{
    const ushort value = character.unicode();
    if ((value >= 0x21 && value <= 0x2F) || (value >= 0x3A && value <= 0x40)
        || (value >= 0x5B && value <= 0x60) || (value >= 0x7B && value <= 0x7E))
        return true;
    switch (character.category()) {
    case QChar::Punctuation_Connector:
    case QChar::Punctuation_Dash:
    case QChar::Punctuation_Open:
    case QChar::Punctuation_Close:
    case QChar::Punctuation_InitialQuote:
    case QChar::Punctuation_FinalQuote:
    case QChar::Punctuation_Other:
    case QChar::Symbol_Math:
    case QChar::Symbol_Currency:
    case QChar::Symbol_Modifier:
    case QChar::Symbol_Other:
        return true;
    default:
        return false;
    }
}

bool isCjk(uint codepoint)
{
    return (codepoint >= 0x4E00 && codepoint <= 0x9FFF)
        || (codepoint >= 0x3400 && codepoint <= 0x4DBF)
        || (codepoint >= 0x20000 && codepoint <= 0x2A6DF)
        || (codepoint >= 0x2A700 && codepoint <= 0x2B73F)
        || (codepoint >= 0x2B740 && codepoint <= 0x2B81F)
        || (codepoint >= 0x2B820 && codepoint <= 0x2CEAF)
        || (codepoint >= 0xF900 && codepoint <= 0xFAFF)
        || (codepoint >= 0x2F800 && codepoint <= 0x2FA1F);
}

QString cleanText(const QString& input)
{
    const QString normalized = input.normalized(QString::NormalizationForm_D).toLower();
    QString result;
    result.reserve(normalized.size());
    for (qsizetype index = 0; index < normalized.size(); ++index) {
        const QChar character = normalized.at(index);
        if (character.category() == QChar::Mark_NonSpacing
            || character.category() == QChar::Mark_SpacingCombining
            || character.category() == QChar::Mark_Enclosing)
            continue;
        if (character == QChar::ReplacementCharacter)
            continue;
        if (isControl(character))
            continue;
        if (isWhitespace(character)) {
            result.append(QChar::fromLatin1(' '));
            continue;
        }
        result.append(character);
    }
    return result;
}

QList<QString> splitIntoWords(const QString& input)
{
    QList<QString> words;
    QString current;
    const auto flush = [&current, &words]() {
        if (!current.isEmpty()) {
            words.append(std::exchange(current, QString{}));
        }
    };
    for (qsizetype index = 0; index < input.size(); ++index) {
        const QChar character = input.at(index);
        const uint codepoint = character.isHighSurrogate() && index + 1 < input.size()
            && input.at(index + 1).isLowSurrogate()
            ? QChar::surrogateToUcs4(character, input.at(++index))
            : character.unicode();
        if (isWhitespace(character)) {
            flush();
        } else if (isPunctuation(character) || isCjk(codepoint)) {
            flush();
            if (character.isHighSurrogate() && index + 1 < input.size()
                && input.at(index + 1).isLowSurrogate()) {
                words.append(input.mid(index, 2));
                ++index;
            } else {
                words.append(QString(character));
            }
        } else {
            current.append(character);
        }
    }
    flush();
    return words;
}

struct Tokenizer final {
    QHash<QString, int> vocabulary;

    std::expected<QList<int>, QString> tokenize(const QString& text) const
    {
        const auto unknown = vocabulary.constFind(QStringLiteral("[UNK]"));
        const auto cls = vocabulary.constFind(QStringLiteral("[CLS]"));
        const auto sep = vocabulary.constFind(QStringLiteral("[SEP]"));
        if (unknown == vocabulary.cend() || cls == vocabulary.cend() || sep == vocabulary.cend())
            return std::unexpected(QStringLiteral("SEARCH_MODEL_LOAD: vocabulary is missing required special tokens"));

        QList<int> output;
        output.reserve(maxTokens);
        output.append(cls.value());
        for (const QString& word : splitIntoWords(cleanText(text))) {
            if (word.size() > 100) {
                output.append(unknown.value());
                continue;
            }
            qsizetype start = 0;
            bool failed = false;
            QList<int> pieces;
            while (start < word.size()) {
                qsizetype end = word.size();
                int id = -1;
                while (start < end) {
                    QString piece = word.mid(start, end - start);
                    if (start > 0)
                        piece.prepend(QStringLiteral("##"));
                    const auto found = vocabulary.constFind(piece);
                    if (found != vocabulary.cend()) {
                        id = found.value();
                        break;
                    }
                    --end;
                }
                if (id < 0) {
                    failed = true;
                    break;
                }
                pieces.append(id);
                start = end;
            }
            if (failed)
                pieces = {unknown.value()};
            if (output.size() + pieces.size() + 1 > maxTokens)
                break;
            output.append(pieces);
        }
        output.append(sep.value());
        return output;
    }
};

} // namespace

struct LocalEmbedder::State final {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "betterflash-search"};
    std::unique_ptr<Ort::Session> session;
    Ort::SessionOptions options;
    Ort::AllocatorWithDefaultOptions allocator;
    struct Tokenizer tokenizer;
    int dimensions = 0;
};

LocalEmbedder::LocalEmbedder() = default;
LocalEmbedder::~LocalEmbedder() = default;
LocalEmbedder::LocalEmbedder(LocalEmbedder&&) noexcept = default;
LocalEmbedder& LocalEmbedder::operator=(LocalEmbedder&&) noexcept = default;

LocalModelSpec LocalEmbedder::spec(const QString& profile)
{
    if (profile == QStringLiteral("compact")) {
        return {QStringLiteral("Xenova/bge-small-en-v1.5"), QStringLiteral("BGE small English v1.5"), 384,
            {{QStringLiteral("model.onnx"),
              QUrl(QStringLiteral("https://huggingface.co/Xenova/bge-small-en-v1.5/resolve/ea104dacec62c0de699686887e3f920caeb4f3e3/onnx/model_quantized.onnx")),
              QByteArrayLiteral("43197c0772cd3622c3ff70f3b42c948d4e32cc3fec11f2022a705f1b8ca80152"), 34014426},
             {QStringLiteral("vocab.txt"),
              QUrl(QStringLiteral("https://huggingface.co/Xenova/bge-small-en-v1.5/resolve/ea104dacec62c0de699686887e3f920caeb4f3e3/vocab.txt")),
              QByteArrayLiteral("07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3"), 231508}}};
    }
    return {QStringLiteral("Xenova/bge-base-en-v1.5"), QStringLiteral("BGE base English v1.5"), 768,
        {{QStringLiteral("model.onnx"),
          QUrl(QStringLiteral("https://huggingface.co/Xenova/bge-base-en-v1.5/resolve/4d6cd88e18e51a5e020c2c305726d76ada9c03cf/onnx/model_quantized.onnx")),
          QByteArrayLiteral("0405fb735849b186fb41f8d433b0079dafc9f84d3da709593565d49568a0926"), 110083337},
         {QStringLiteral("vocab.txt"),
          QUrl(QStringLiteral("https://huggingface.co/Xenova/bge-base-en-v1.5/resolve/4d6cd88e18e51a5e020c2c305726d76ada9c03cf/vocab.txt")),
          QByteArrayLiteral("07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3"), 231508}}};
}

std::expected<void, QString> LocalEmbedder::load(const QString& directory, const QString& profile)
{
    const struct LocalModelSpec model = spec(profile);
    const auto verifyAsset = [&directory](const struct ModelAsset& asset) -> std::expected<void, QString> {
        const QString path = directory + QStringLiteral("/") + asset.filename;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("SEARCH_MODEL_LOAD: cannot verify local embedding asset: %1").arg(file.errorString()));
        if (file.size() != asset.bytes)
            return std::unexpected(QStringLiteral("SEARCH_MODEL_LOAD: local embedding asset has unexpected size: %1").arg(asset.filename));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd())
            hash.addData(file.read(1024 * 1024));
        const QByteArray digest = hash.result().toHex();
        if (digest != asset.sha256)
            return std::unexpected(QStringLiteral("SEARCH_MODEL_LOAD: local embedding asset failed SHA-256 verification: %1").arg(asset.filename));
        return {};
    };
    for (const struct ModelAsset& asset : model.assets) {
        const auto verified = verifyAsset(asset);
        if (!verified)
            return std::unexpected(verified.error());
    }
    const QString vocabularyPath = directory + QStringLiteral("/vocab.txt");
    QFile vocabularyFile(vocabularyPath);
    if (!vocabularyFile.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("SEARCH_MODEL_LOAD: cannot open local embedding vocabulary: %1").arg(vocabularyFile.errorString()));

    std::unique_ptr<struct State> state;
    const QString modelPath = directory + QStringLiteral("/model.onnx");
    if (!QFile::exists(modelPath))
        return std::unexpected(QStringLiteral("SEARCH_MODEL_LOAD: cannot find local embedding model: %1").arg(modelPath));
    try {
        state = std::make_unique<struct State>();
        const QList<QByteArray> lines = vocabularyFile.readAll().split('\n');
        for (qsizetype index = 0; index < lines.size(); ++index) {
            const QString token = QString::fromUtf8(lines.at(index)).trimmed();
            if (!token.isEmpty())
                state->tokenizer.vocabulary.insert(token, static_cast<int>(index));
        }
        if (state->tokenizer.vocabulary.isEmpty())
            return std::unexpected(QStringLiteral("SEARCH_MODEL_LOAD: local embedding vocabulary is empty"));
        state->options.SetIntraOpNumThreads(profile == QStringLiteral("compact") ? 2 : 4);
        state->options.SetInterOpNumThreads(1);
        state->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const QByteArray nativeModelPath = modelPath.toLocal8Bit();
        state->session = std::make_unique<Ort::Session>(state->environment, nativeModelPath.constData(), state->options);
        state->dimensions = model.dimensions;
    } catch (const Ort::Exception& error) {
        return std::unexpected(QStringLiteral("SEARCH_MODEL_LOAD: cannot load local embedding model: %1").arg(QString::fromUtf8(error.what())));
    }
    m_state = std::move(state);
    return {};
}

std::expected<QVector<float>, QString> LocalEmbedder::encode(const QString& text, bool query) const
{
    if (!m_state || !m_state->session)
        return std::unexpected(QStringLiteral("SEARCH_MODEL_INFERENCE: local embedding model is not loaded"));
    const QString input = query ? QString::fromLatin1(queryPrefix) + text : text;
    const auto tokenResult = m_state->tokenizer.tokenize(input);
    if (!tokenResult)
        return std::unexpected(QStringLiteral("SEARCH_MODEL_INFERENCE: %1").arg(tokenResult.error()));
    const QList<int> tokens = *tokenResult;
    const int sequenceLength = static_cast<int>(tokens.size());
    std::vector<int64_t> ids(sequenceLength), mask(sequenceLength, 1), types(sequenceLength, 0);
    for (int index = 0; index < sequenceLength; ++index)
        ids[index] = tokens.at(index);
    for (int index = sequenceLength; index < maxTokens; ++index) {
        ids.push_back(0);
        mask.push_back(0);
        types.push_back(0);
    }
    try {
        const std::array<int64_t, 2> shape{1, maxTokens};
        const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const auto makeTensor = [&memoryInfo, &shape](std::vector<int64_t>& values) {
            return Ort::Value::CreateTensor<int64_t>(memoryInfo, values.data(), values.size(), shape.data(), shape.size());
        };
        std::array<Ort::Value, 3> inputs{makeTensor(ids), makeTensor(mask), makeTensor(types)};
        const char* inputNames[] = {"input_ids", "attention_mask", "token_type_ids"};
        const char* outputNames[] = {"last_hidden_state"};
        const auto output = m_state->session->Run(Ort::RunOptions{nullptr}, inputNames, inputs.data(), inputs.size(), outputNames, 1);
        if (output.size() != 1 || !output.front().IsTensor())
            return std::unexpected(QStringLiteral("SEARCH_MODEL_INFERENCE: local embedding model returned no tensor output"));
        const auto typeInfo = output.front().GetTensorTypeAndShapeInfo();
        const auto dimensions = typeInfo.GetShape();
        if (dimensions.size() != 3 || dimensions[0] != 1 || dimensions[1] != maxTokens || dimensions[2] != m_state->dimensions)
            return std::unexpected(QStringLiteral("SEARCH_MODEL_INFERENCE: local embedding model returned an unexpected tensor shape"));
        const float* values = output.front().GetTensorData<float>();
        QVector<float> embedding(m_state->dimensions);
        double norm = 0.0;
        for (int index = 0; index < m_state->dimensions; ++index) {
            embedding[index] = values[index];
            norm += static_cast<double>(embedding[index]) * embedding[index];
        }
        if (!(norm > std::numeric_limits<double>::epsilon()))
            return std::unexpected(QStringLiteral("SEARCH_MODEL_INFERENCE: local embedding model returned a zero vector"));
        const float inverseNorm = static_cast<float>(1.0 / std::sqrt(norm));
        for (float& value : embedding)
            value *= inverseNorm;
        return embedding;
    } catch (const Ort::Exception& error) {
        return std::unexpected(QStringLiteral("SEARCH_MODEL_INFERENCE: local embedding inference failed: %1").arg(QString::fromUtf8(error.what())));
    }
}
