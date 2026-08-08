#include "RasterRenderer.h"

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>

#include <gdal_priv.h>
#include <gdal_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace {

using DatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;

struct RasterViewportBuffer
{
    int width = 0;
    int height = 0;
    int bandCount = 0;
    QVector<float> samples;
    QVector<float> alpha;

    [[nodiscard]] qsizetype byteSize() const noexcept
    {
        return (samples.size() + alpha.size())
               * static_cast<qsizetype>(sizeof(float));
    }
};

QMutex s_cacheMutex;
QHash<QString, std::shared_ptr<RasterViewportBuffer>> s_cache;
QList<QString> s_cacheOrder;
qsizetype s_cacheBytes = 0;
constexpr qsizetype kCacheLimit = 256LL * 1024LL * 1024LL;

double bandMinimum(const LayerSnapshot &layer, int band)
{
    const int index = band - 1;
    return index >= 0 && index < layer.bandMinimums.size()
        ? layer.bandMinimums.at(index) : 0.0;
}

double bandMaximum(const LayerSnapshot &layer, int band)
{
    const int index = band - 1;
    return index >= 0 && index < layer.bandMaximums.size()
        ? layer.bandMaximums.at(index) : 255.0;
}

QStringList colorRampColors(const QString &name)
{
    static const QHash<QString, QStringList> ramps {
        {QStringLiteral("Viridis"),
         {QStringLiteral("#440154"), QStringLiteral("#3b528b"),
          QStringLiteral("#21918c"), QStringLiteral("#5ec962"),
          QStringLiteral("#fde725")}},
        {QStringLiteral("Plasma"),
         {QStringLiteral("#0d0887"), QStringLiteral("#7e03a8"),
          QStringLiteral("#cc4778"), QStringLiteral("#f89540"),
          QStringLiteral("#f0f921")}},
        {QStringLiteral("Inferno"),
         {QStringLiteral("#000004"), QStringLiteral("#420a68"),
          QStringLiteral("#932667"), QStringLiteral("#dd513a"),
          QStringLiteral("#fca50a"), QStringLiteral("#fcffa4")}},
        {QStringLiteral("Magma"),
         {QStringLiteral("#000004"), QStringLiteral("#51127c"),
          QStringLiteral("#b73779"), QStringLiteral("#fc8961"),
          QStringLiteral("#fcfdbf")}},
        {QStringLiteral("Cividis"),
         {QStringLiteral("#00224e"), QStringLiteral("#2e4a7d"),
          QStringLiteral("#576d8c"), QStringLiteral("#848e88"),
          QStringLiteral("#b7ad6f"), QStringLiteral("#fee838")}},
        {QStringLiteral("Turbo"),
         {QStringLiteral("#30123b"), QStringLiteral("#4666e5"),
          QStringLiteral("#28bbec"), QStringLiteral("#32f298"),
          QStringLiteral("#a4fc3c"), QStringLiteral("#f9ba38"),
          QStringLiteral("#e85d0b"), QStringLiteral("#7a0403")}},
        {QStringLiteral("Terrain"),
         {QStringLiteral("#1d4e89"), QStringLiteral("#5aa9e6"),
          QStringLiteral("#9fd356"), QStringLiteral("#a98467"),
          QStringLiteral("#f5f5f5")}},
        {QStringLiteral("Gray"),
         {QStringLiteral("#111111"), QStringLiteral("#f5f5f5")}}
    };
    return ramps.value(name, ramps.value(QStringLiteral("Viridis")));
}

QString cacheKey(const LayerSnapshot &layer,
                 const RasterRenderViewport &viewport,
                 const QVector<int> &bands)
{
    QStringList parts {
        layer.sourceUri.isEmpty() ? layer.path : layer.sourceUri,
        viewport.coordinateMode,
        QString::number(QFileInfo(layer.path).lastModified().toMSecsSinceEpoch()),
        QString::number(viewport.width),
        QString::number(viewport.height),
        QString::number(viewport.minMercatorX, 'g', 16),
        QString::number(viewport.minMercatorY, 'g', 16),
        QString::number(viewport.maxMercatorX, 'g', 16),
        QString::number(viewport.maxMercatorY, 'g', 16),
        layer.noDataEnabled ? layer.noDataValue : QStringLiteral("disabled")
    };
    for (const int band : bands)
        parts.push_back(QString::number(band));
    return parts.join(QLatin1Char('|'));
}

std::shared_ptr<RasterViewportBuffer> cachedBuffer(const QString &key)
{
    QMutexLocker locker(&s_cacheMutex);
    const auto buffer = s_cache.value(key);
    if (buffer) {
        s_cacheOrder.removeAll(key);
        s_cacheOrder.push_back(key);
    }
    return buffer;
}

void insertBuffer(const QString &key,
                  const std::shared_ptr<RasterViewportBuffer> &buffer)
{
    QMutexLocker locker(&s_cacheMutex);
    if (const auto previous = s_cache.take(key); previous)
        s_cacheBytes -= previous->byteSize();
    s_cacheOrder.removeAll(key);
    s_cache.insert(key, buffer);
    s_cacheOrder.push_back(key);
    s_cacheBytes += buffer->byteSize();
    while (s_cacheBytes > kCacheLimit && s_cacheOrder.size() > 1) {
        const QString oldest = s_cacheOrder.takeFirst();
        if (const auto removed = s_cache.take(oldest); removed)
            s_cacheBytes -= removed->byteSize();
    }
}

std::shared_ptr<RasterViewportBuffer>
readViewport(const LayerSnapshot &layer,
             const RasterRenderViewport &viewport, QString &error)
{
    const QVector<int> selectedBands =
        layer.rasterMode == QStringLiteral("single")
        ? QVector<int> {layer.grayBand}
        : QVector<int> {layer.redBand, layer.greenBand, layer.blueBand};
    const QString key = cacheKey(layer, viewport, selectedBands);
    if (auto cached = cachedBuffer(key))
        return cached;

    const QString rasterSource =
        layer.sourceUri.isEmpty() ? layer.path : layer.sourceUri;
    DatasetPtr source(
        static_cast<GDALDataset *>(GDALOpenEx(
            rasterSource.toUtf8().constData(),
            GDAL_OF_RASTER | GDAL_OF_READONLY,
            nullptr, nullptr, nullptr)),
        GDALClose);
    if (!source) {
        error = QStringLiteral("GDAL cannot open %1").arg(rasterSource);
        return {};
    }

    if (viewport.coordinateMode == QStringLiteral("pixel")) {
        const qsizetype pixelCount =
            static_cast<qsizetype>(viewport.width) * viewport.height;
        auto buffer = std::make_shared<RasterViewportBuffer>();
        buffer->width = viewport.width;
        buffer->height = viewport.height;
        buffer->bandCount = selectedBands.size();
        buffer->samples.fill(
            std::numeric_limits<float>::quiet_NaN(),
            pixelCount * selectedBands.size());
        buffer->alpha.fill(0.0F, pixelCount);

        const double spanX =
            viewport.maxMercatorX - viewport.minMercatorX;
        const double spanY =
            viewport.maxMercatorY - viewport.minMercatorY;
        const int sourceX0 = std::clamp(
            static_cast<int>(std::floor(viewport.minMercatorX)),
            0, source->GetRasterXSize());
        const int sourceY0 = std::clamp(
            static_cast<int>(std::floor(viewport.minMercatorY)),
            0, source->GetRasterYSize());
        const int sourceX1 = std::clamp(
            static_cast<int>(std::ceil(viewport.maxMercatorX)),
            0, source->GetRasterXSize());
        const int sourceY1 = std::clamp(
            static_cast<int>(std::ceil(viewport.maxMercatorY)),
            0, source->GetRasterYSize());
        if (sourceX1 <= sourceX0 || sourceY1 <= sourceY0
            || spanX <= 0.0 || spanY <= 0.0) {
            insertBuffer(key, buffer);
            return buffer;
        }

        const int destinationX0 = std::clamp(
            static_cast<int>(std::lround(
                (sourceX0 - viewport.minMercatorX) / spanX
                * viewport.width)),
            0, viewport.width - 1);
        const int destinationY0 = std::clamp(
            static_cast<int>(std::lround(
                (sourceY0 - viewport.minMercatorY) / spanY
                * viewport.height)),
            0, viewport.height - 1);
        const int destinationX1 = std::clamp(
            static_cast<int>(std::lround(
                (sourceX1 - viewport.minMercatorX) / spanX
                * viewport.width)),
            destinationX0 + 1, viewport.width);
        const int destinationY1 = std::clamp(
            static_cast<int>(std::lround(
                (sourceY1 - viewport.minMercatorY) / spanY
                * viewport.height)),
            destinationY0 + 1, viewport.height);
        const int destinationWidth = destinationX1 - destinationX0;
        const int destinationHeight = destinationY1 - destinationY0;
        const qsizetype destinationPixelCount =
            static_cast<qsizetype>(destinationWidth) * destinationHeight;

        for (int outputBand = 0; outputBand < selectedBands.size();
             ++outputBand) {
            GDALRasterBand *band =
                source->GetRasterBand(selectedBands.at(outputBand));
            if (!band) {
                error = QStringLiteral("GDAL cannot open a selected array band");
                return {};
            }
            QVector<float> samples(destinationPixelCount);
            GDALRasterIOExtraArg arguments;
            INIT_RASTERIO_EXTRA_ARG(arguments);
            arguments.eResampleAlg = GRIORA_Bilinear;
            if (band->RasterIO(
                    GF_Read, sourceX0, sourceY0,
                    sourceX1 - sourceX0, sourceY1 - sourceY0,
                    samples.data(), destinationWidth, destinationHeight,
                    GDT_Float32, 0, 0, &arguments) != CE_None) {
                error = QStringLiteral("GDAL cannot read the current array window");
                return {};
            }
            for (int y = 0; y < destinationHeight; ++y) {
                const qsizetype sourceOffset =
                    static_cast<qsizetype>(y) * destinationWidth;
                const qsizetype destinationOffset =
                    static_cast<qsizetype>(destinationY0 + y)
                        * viewport.width
                    + destinationX0;
                std::copy_n(
                    samples.cbegin() + sourceOffset, destinationWidth,
                    buffer->samples.begin()
                        + outputBand * pixelCount + destinationOffset);
            }
        }

        bool noDataIsNumber = false;
        const double configuredNoData =
            layer.noDataValue.toDouble(&noDataIsNumber);
        const bool noDataIsNaN =
            layer.noDataValue.compare(QStringLiteral("nan"),
                                      Qt::CaseInsensitive) == 0;
        for (int y = destinationY0; y < destinationY1; ++y) {
            for (int x = destinationX0; x < destinationX1; ++x) {
                const qsizetype index =
                    static_cast<qsizetype>(y) * viewport.width + x;
                bool transparent = false;
                if (layer.noDataEnabled) {
                    transparent = true;
                    for (int band = 0; band < selectedBands.size(); ++band) {
                        const float value =
                            buffer->samples.at(band * pixelCount + index);
                        const bool isNoData =
                            (noDataIsNaN && std::isnan(value))
                            || (noDataIsNumber
                                && static_cast<double>(value)
                                       == configuredNoData);
                        if (!isNoData) {
                            transparent = false;
                            break;
                        }
                    }
                }
                buffer->alpha[index] = transparent ? 0.0F : 255.0F;
            }
        }
        insertBuffer(key, buffer);
        return buffer;
    }

    QStringList optionStrings {
        QStringLiteral("-of"), QStringLiteral("MEM"),
        QStringLiteral("-ot"), QStringLiteral("Float32"),
        QStringLiteral("-s_srs"), layer.srs,
        QStringLiteral("-t_srs"), QStringLiteral("EPSG:3857"),
        QStringLiteral("-te_srs"), QStringLiteral("EPSG:3857"),
        QStringLiteral("-te"),
        QString::number(viewport.minMercatorX, 'g', 16),
        QString::number(viewport.minMercatorY, 'g', 16),
        QString::number(viewport.maxMercatorX, 'g', 16),
        QString::number(viewport.maxMercatorY, 'g', 16),
        QStringLiteral("-ts"), QString::number(viewport.width),
        QString::number(viewport.height),
        QStringLiteral("-r"), QStringLiteral("bilinear"),
        QStringLiteral("-ovr"), QStringLiteral("AUTO"),
        QStringLiteral("-wo"), QStringLiteral("NUM_THREADS=ALL_CPUS"),
        QStringLiteral("-wm"), QStringLiteral("64"),
        QStringLiteral("-dstalpha")
    };
    for (const int band : selectedBands)
        optionStrings << QStringLiteral("-srcband") << QString::number(band);
    if (layer.noDataEnabled)
        optionStrings << QStringLiteral("-srcnodata") << layer.noDataValue;

    QVector<QByteArray> encoded;
    encoded.reserve(optionStrings.size());
    for (const auto &option : optionStrings)
        encoded.push_back(option.toUtf8());
    QVector<char *> arguments;
    arguments.reserve(encoded.size() + 1);
    for (auto &option : encoded)
        arguments.push_back(option.data());
    arguments.push_back(nullptr);

    std::unique_ptr<GDALWarpAppOptions, decltype(&GDALWarpAppOptionsFree)> options(
        GDALWarpAppOptionsNew(arguments.data(), nullptr),
        GDALWarpAppOptionsFree);
    if (!options) {
        error = QStringLiteral("GDAL cannot create warp options");
        return {};
    }

    GDALDatasetH sourceHandle = source.get();
    int usageError = FALSE;
    DatasetPtr warped(
        static_cast<GDALDataset *>(
            GDALWarp("", nullptr, 1, &sourceHandle, options.get(), &usageError)),
        GDALClose);
    if (!warped || usageError) {
        error = QStringLiteral("GDAL cannot render the current raster viewport");
        return {};
    }

    const qsizetype pixelCount =
        static_cast<qsizetype>(viewport.width) * viewport.height;
    auto buffer = std::make_shared<RasterViewportBuffer>();
    buffer->width = viewport.width;
    buffer->height = viewport.height;
    buffer->bandCount = selectedBands.size();
    buffer->samples.resize(pixelCount * selectedBands.size());
    buffer->alpha.fill(255.0F, pixelCount);

    for (int outputBand = 0; outputBand < selectedBands.size(); ++outputBand) {
        GDALRasterBand *band = warped->GetRasterBand(outputBand + 1);
        if (!band || band->RasterIO(
                GF_Read, 0, 0, viewport.width, viewport.height,
                buffer->samples.data() + pixelCount * outputBand,
                viewport.width, viewport.height, GDT_Float32,
                0, 0, nullptr) != CE_None) {
            error = QStringLiteral("GDAL cannot read a rendered raster band");
            return {};
        }
    }
    if (warped->GetRasterCount() > selectedBands.size()) {
        if (GDALRasterBand *alphaBand =
                warped->GetRasterBand(selectedBands.size() + 1)) {
            if (alphaBand->RasterIO(
                    GF_Read, 0, 0, viewport.width, viewport.height,
                    buffer->alpha.data(), viewport.width, viewport.height,
                    GDT_Float32, 0, 0, nullptr) != CE_None) {
                error = QStringLiteral("GDAL cannot read the raster mask");
                return {};
            }
        }
    }
    insertBuffer(key, buffer);
    return buffer;
}

struct StretchTransform
{
    double minimum = 0.0;
    double maximum = 255.0;
    QVector<double> cumulativeDistribution;

    [[nodiscard]] double normalized(float value) const
    {
        if (!std::isfinite(value) || maximum <= minimum)
            return 0.0;
        double position =
            std::clamp((static_cast<double>(value) - minimum)
                           / (maximum - minimum),
                       0.0, 1.0);
        if (!cumulativeDistribution.isEmpty()) {
            const int last =
                static_cast<int>(cumulativeDistribution.size()) - 1;
            const int bin = std::clamp(
                static_cast<int>(std::floor(position * last)), 0, last);
            position = cumulativeDistribution.at(bin);
        }
        return position;
    }

    [[nodiscard]] int byte(float value) const
    {
        return static_cast<int>(std::lround(normalized(value) * 255.0));
    }
};

StretchTransform makeStretchTransform(
    const LayerSnapshot &layer, const RasterViewportBuffer &buffer,
    qsizetype sampleOffset, double configuredMinimum,
    double configuredMaximum)
{
    StretchTransform transform {configuredMinimum, configuredMaximum, {}};
    // 除手动最小值–最大值外，其余拉伸都只统计已经缓存的当前视窗，
    // 不重新打开或扫描整幅 GeoTIFF，因此切换方式能够即时反馈。
    if (layer.stretchMode == QStringLiteral("minmax"))
        return transform;

    const qsizetype pixelCount =
        static_cast<qsizetype>(buffer.width) * buffer.height;
    double actualMinimum = std::numeric_limits<double>::infinity();
    double actualMaximum = -std::numeric_limits<double>::infinity();
    double mean = 0.0;
    double squaredDifference = 0.0;
    qsizetype count = 0;
    for (qsizetype index = 0; index < pixelCount; ++index) {
        if (buffer.alpha.at(index) <= 0.0F)
            continue;
        const float sample = buffer.samples.at(sampleOffset + index);
        if (!std::isfinite(sample))
            continue;
        const double value = sample;
        actualMinimum = std::min(actualMinimum, value);
        actualMaximum = std::max(actualMaximum, value);
        ++count;
        const double delta = value - mean;
        mean += delta / static_cast<double>(count);
        squaredDifference += delta * (value - mean);
    }
    if (count == 0 || !(actualMinimum < actualMaximum))
        return transform;

    if (layer.stretchMode == QStringLiteral("standard_deviation")) {
        const double deviation =
            count > 1
            ? std::sqrt(squaredDifference / static_cast<double>(count - 1))
            : 0.0;
        transform.minimum =
            std::max(actualMinimum, mean - 2.0 * deviation);
        transform.maximum =
            std::min(actualMaximum, mean + 2.0 * deviation);
        if (!(transform.minimum < transform.maximum)) {
            transform.minimum = actualMinimum;
            transform.maximum = actualMaximum;
        }
        return transform;
    }

    constexpr int kHistogramBins = 1024;
    QVector<quint64> histogram(kHistogramBins, 0);
    const double span = actualMaximum - actualMinimum;
    for (qsizetype index = 0; index < pixelCount; ++index) {
        if (buffer.alpha.at(index) <= 0.0F)
            continue;
        const float sample = buffer.samples.at(sampleOffset + index);
        if (!std::isfinite(sample))
            continue;
        const int bin = std::clamp(
            static_cast<int>(std::floor(
                (static_cast<double>(sample) - actualMinimum)
                / span * (kHistogramBins - 1))),
            0, kHistogramBins - 1);
        ++histogram[bin];
    }

    if (layer.stretchMode == QStringLiteral("percent_clip")) {
        const quint64 lowTarget =
            static_cast<quint64>(std::floor(count * 0.02));
        const quint64 highTarget =
            static_cast<quint64>(std::ceil(count * 0.98));
        quint64 cumulative = 0;
        int lowBin = 0;
        int highBin = kHistogramBins - 1;
        bool foundLow = false;
        for (int bin = 0; bin < kHistogramBins; ++bin) {
            cumulative += histogram.at(bin);
            if (!foundLow && cumulative >= lowTarget) {
                lowBin = bin;
                foundLow = true;
            }
            if (cumulative >= highTarget) {
                highBin = bin;
                break;
            }
        }
        transform.minimum =
            actualMinimum + span * lowBin / (kHistogramBins - 1);
        transform.maximum =
            actualMinimum + span * highBin / (kHistogramBins - 1);
        if (!(transform.minimum < transform.maximum)) {
            transform.minimum = actualMinimum;
            transform.maximum = actualMaximum;
        }
        return transform;
    }

    if (layer.stretchMode == QStringLiteral("histogram_equalization")) {
        transform.minimum = actualMinimum;
        transform.maximum = actualMaximum;
        transform.cumulativeDistribution.resize(kHistogramBins);
        quint64 cumulative = 0;
        quint64 firstNonZero = 0;
        for (int bin = 0; bin < kHistogramBins; ++bin) {
            cumulative += histogram.at(bin);
            if (firstNonZero == 0 && cumulative > 0)
                firstNonZero = cumulative;
            const quint64 denominator =
                static_cast<quint64>(count) - firstNonZero;
            transform.cumulativeDistribution[bin] =
                denominator > 0 && cumulative >= firstNonZero
                ? static_cast<double>(cumulative - firstNonZero)
                      / static_cast<double>(denominator)
                : 0.0;
        }
    }
    return transform;
}

QColor rampColor(const QStringList &colors, double normalized)
{
    if (colors.isEmpty())
        return {};
    const int lastIndex = static_cast<int>(colors.size()) - 1;
    const double position =
        std::clamp(normalized, 0.0, 1.0) * lastIndex;
    const int left = std::clamp(static_cast<int>(std::floor(position)),
                                0, lastIndex);
    const int right = std::min(left + 1, lastIndex);
    const double fraction = position - left;
    const QColor a(colors.at(left));
    const QColor b(colors.at(right));
    return QColor::fromRgbF(
        a.redF() + (b.redF() - a.redF()) * fraction,
        a.greenF() + (b.greenF() - a.greenF()) * fraction,
        a.blueF() + (b.blueF() - a.blueF()) * fraction);
}

QImage colorize(const LayerSnapshot &layer,
                const RasterViewportBuffer &buffer)
{
    QImage image(buffer.width, buffer.height,
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const qsizetype pixelCount =
        static_cast<qsizetype>(buffer.width) * buffer.height;
    const QStringList colors = colorRampColors(layer.colorRamp);
    const double opacity = std::clamp(layer.opacity, 0.0, 1.0);
    StretchTransform singleTransform;
    std::array<StretchTransform, 3> rgbTransforms;
    if (layer.rasterMode == QStringLiteral("single")) {
        singleTransform = makeStretchTransform(
            layer, buffer, 0,
            bandMinimum(layer, layer.grayBand),
            bandMaximum(layer, layer.grayBand));
    } else {
        rgbTransforms[0] = makeStretchTransform(
            layer, buffer, 0,
            bandMinimum(layer, layer.redBand),
            bandMaximum(layer, layer.redBand));
        rgbTransforms[1] = makeStretchTransform(
            layer, buffer, pixelCount,
            bandMinimum(layer, layer.greenBand),
            bandMaximum(layer, layer.greenBand));
        rgbTransforms[2] = makeStretchTransform(
            layer, buffer, 2 * pixelCount,
            bandMinimum(layer, layer.blueBand),
            bandMaximum(layer, layer.blueBand));
    }

    for (int y = 0; y < buffer.height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < buffer.width; ++x) {
            const qsizetype index =
                static_cast<qsizetype>(y) * buffer.width + x;
            const int alpha = std::clamp(
                static_cast<int>(std::lround(buffer.alpha.at(index) * opacity)),
                0, 255);
            if (alpha == 0)
                continue;

            int red = 0;
            int green = 0;
            int blue = 0;
            if (layer.rasterMode == QStringLiteral("single")) {
                const float value = buffer.samples.at(index);
                if (!std::isfinite(value))
                    continue;
                double normalized = singleTransform.normalized(value);
                if (layer.colorRampReversed)
                    normalized = 1.0 - normalized;
                const QColor color = rampColor(colors, normalized);
                red = color.red();
                green = color.green();
                blue = color.blue();
            } else {
                red = rgbTransforms[0].byte(buffer.samples.at(index));
                green = rgbTransforms[1].byte(
                    buffer.samples.at(pixelCount + index));
                blue = rgbTransforms[2].byte(
                    buffer.samples.at(2 * pixelCount + index));
            }
            line[x] = qPremultiply(qRgba(red, green, blue, alpha));
        }
    }
    return image;
}

} // namespace

RasterRenderResult renderRasterLayer(
    const LayerSnapshot &layer, const RasterRenderViewport &viewport)
{
    RasterRenderResult result;
    if (viewport.width <= 0 || viewport.height <= 0)
        return result;
    if (const auto buffer = readViewport(layer, viewport, result.error))
        result.image = colorize(layer, *buffer);
    return result;
}
