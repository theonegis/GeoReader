#include "AppController.h"
#include "MapCanvas.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>
#include <QTranslator>

#include <cpl_conv.h>
#include <gdal.h>
#include <mapnik/datasource_cache.hpp>

namespace {

[[nodiscard]] QStringList runtimeResourceCandidates(const QString &leaf)
{
    const QDir executableDirectory(QCoreApplication::applicationDirPath());
    return {
        executableDirectory.filePath(QStringLiteral("../Resources/") + leaf),
        executableDirectory.filePath(QStringLiteral("../share/georeader/") + leaf),
    };
}

void configureGdalData()
{
    for (const QString &path :
         runtimeResourceCandidates(QStringLiteral("gdal"))) {
        if (QFileInfo::exists(path)) {
            CPLSetConfigOption("GDAL_DATA",
                               QDir::toNativeSeparators(path).toUtf8().constData());
            break;
        }
    }

    for (const QString &path :
         runtimeResourceCandidates(QStringLiteral("proj"))) {
        if (QFileInfo::exists(path)) {
            const QByteArray nativePath =
                QDir::toNativeSeparators(path).toUtf8();
            CPLSetConfigOption("PROJ_DATA", nativePath.constData());
            CPLSetConfigOption("PROJ_LIB", nativePath.constData());
            break;
        }
    }
}

void registerMapnikInputPlugins()
{
    const QDir executableDirectory(QCoreApplication::applicationDirPath());
    const QStringList candidates {
        executableDirectory.filePath(
            QString::fromUtf8(GEOREADER_MAPNIK_RUNTIME_INPUT_DIR)),
        executableDirectory.filePath(
            QStringLiteral("../PlugIns/mapnik/input")),
        executableDirectory.filePath(QStringLiteral("mapnik/input")),
        executableDirectory.filePath(
            QStringLiteral("../lib/georeader/mapnik/input")),
        QString::fromUtf8(GEOREADER_MAPNIK_INPUT_DIR),
    };

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            mapnik::datasource_cache::instance().register_datasources(
                path.toStdString());
        }
    }
#if defined(__clang__)
#  pragma clang diagnostic pop
#endif
}

void applyLanguage(QApplication &application, QTranslator &translator,
                   const QString &language)
{
    application.removeTranslator(&translator);
    const bool english =
        language.startsWith(QStringLiteral("en"), Qt::CaseInsensitive);
    QLocale::setDefault(QLocale(english ? QLocale::English
                                       : QLocale::Chinese,
                                english ? QLocale::UnitedStates
                                        : QLocale::China));
    if (english
        && translator.load(QStringLiteral(":/i18n/georeader_en.qm"))) {
        application.installTranslator(&translator);
    }
}

void configureLinuxPlatform()
{
#if defined(Q_OS_LINUX)
    // 尊重用户或桌面启动器的显式选择。检测到 Wayland 会话时按顺序尝试
    // 原生 Wayland 与 XCB；Qt 的分号列表会选择第一个可用 QPA 插件，
    // 因而缺少 Wayland 插件时仍可通过 XWayland/X11 启动。
    if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        return;
    const bool waylandSession =
        qEnvironmentVariable("XDG_SESSION_TYPE")
            .compare(QStringLiteral("wayland"), Qt::CaseInsensitive) == 0
        || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    if (waylandSession)
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland;xcb"));
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(QStringLiteral("GeoReader"));
    QApplication::setApplicationName(QStringLiteral("GeoReader"));
    QApplication::setApplicationVersion(QString::fromLatin1(GEOREADER_VERSION));
    configureLinuxPlatform();
    QApplication application(argc, argv);
    QApplication::setWindowIcon(
        QIcon(QStringLiteral(":/icons/georeader.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Modern desktop spatial data viewer"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Load the supplied files and exit after a short runtime check."));
    const QCommandLineOption screenshotOption(
        QStringList {QStringLiteral("s"), QStringLiteral("screenshot")},
        QStringLiteral("Save a window screenshot and exit."),
        QStringLiteral("path"));
    const QCommandLineOption panelOption(
        QStringLiteral("panel"),
        QStringLiteral("Open a panel for UI testing (layers, raster, vector, settings)."),
        QStringLiteral("name"));
    const QCommandLineOption selectVectorOption(
        QStringLiteral("select-vector"),
        QStringLiteral("Select a vector feature for UI testing: row,longitude,latitude."),
        QStringLiteral("row,longitude,latitude"));
    const QCommandLineOption metadataRowOption(
        QStringLiteral("metadata-row"),
        QStringLiteral("Open metadata for a layer row during UI testing."),
        QStringLiteral("row"));
    const QCommandLineOption attributeTableRowOption(
        QStringLiteral("attribute-table-row"),
        QStringLiteral("Open the attribute table for a vector layer row during UI testing."),
        QStringLiteral("row"));
    parser.addOption(smokeTestOption);
    parser.addOption(screenshotOption);
    parser.addOption(panelOption);
    parser.addOption(selectVectorOption);
    parser.addOption(metadataRowOption);
    parser.addOption(attributeTableRowOption);
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("Spatial data files to open."),
                                 QStringLiteral("[files...]"));
    parser.process(application);

    QQuickStyle::setStyle(AppController::savedOrPlatformStyle());
    QTranslator translator;
    applyLanguage(application, translator,
                  AppController::savedOrSystemLanguage());
    configureGdalData();
    registerMapnikInputPlugins();
    GDALAllRegister();

    qmlRegisterType<MapCanvas>("GeoReader", 1, 0, "MapCanvas");

    AppController controller;
    QFont font = application.font();
    font.setFamily(controller.fontFamily());
    font.setPointSize(controller.fontSize());
    application.setFont(font);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    QObject::connect(
        &controller, &AppController::languageChanged, &engine,
        [&application, &translator, &controller, &engine] {
            applyLanguage(application, translator, controller.language());
            engine.retranslate();
        });
    engine.loadFromModule(QStringLiteral("GeoReader"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;
    if (parser.isSet(panelOption)) {
        engine.rootObjects().first()->setProperty(
            "activePanel", parser.value(panelOption));
    }
    const QStringList arguments = parser.positionalArguments();
    if (!arguments.isEmpty()) {
        QTimer::singleShot(0, &controller, [&controller, arguments] {
            controller.loadFiles(arguments);
        });
    }
    if (parser.isSet(selectVectorOption)) {
        const QStringList selection =
            parser.value(selectVectorOption).split(u',');
        if (selection.size() == 3) {
            bool rowOk = false;
            bool longitudeOk = false;
            bool latitudeOk = false;
            const int row = selection.at(0).toInt(&rowOk);
            const double longitude = selection.at(1).toDouble(&longitudeOk);
            const double latitude = selection.at(2).toDouble(&latitudeOk);
            if (rowOk && longitudeOk && latitudeOk) {
                QTimer::singleShot(
                    500, &controller,
                    [&controller, &engine, row, longitude, latitude] {
                    const QVariantMap result =
                        controller.queryVector(row, longitude, latitude, 0.01);
                    QObject *root = engine.rootObjects().value(0);
                    if (!root)
                        return;
                    root->setProperty("selectedVectorLayer", row);
                    root->setProperty("vectorResult", result);
                    if (auto *canvas =
                            root->findChild<MapCanvas *>(
                                QStringLiteral("mapCanvas"))) {
                        canvas->setSelectedFeatureWkt(
                            result.value(QStringLiteral("geometryWkt"))
                                .toString());
                    }
                });
            }
        }
    }
    const auto invokeLayerDialog =
        [&parser, &engine](const QCommandLineOption &option,
                          const char *method) {
            if (!parser.isSet(option))
                return;
            bool rowOk = false;
            const int row = parser.value(option).toInt(&rowOk);
            if (!rowOk)
                return;
            QTimer::singleShot(600, &engine, [&engine, row, method] {
                if (QObject *root = engine.rootObjects().value(0)) {
                    QMetaObject::invokeMethod(
                        root, method, Q_ARG(QVariant, QVariant(row)));
                }
            });
        };
    invokeLayerDialog(metadataRowOption, "showMetadata");
    invokeLayerDialog(attributeTableRowOption, "showAttributeTable");
    if (parser.isSet(screenshotOption)) {
        const QString outputPath =
            QFileInfo(parser.value(screenshotOption)).absoluteFilePath();
        QTimer::singleShot(2500, &application,
                           [&application, &engine, outputPath] {
            if (auto *window = qobject_cast<QQuickWindow *>(
                    engine.rootObjects().value(0))) {
                window->grabWindow().save(outputPath);
            }
            application.quit();
        });
    } else if (parser.isSet(smokeTestOption)) {
        QTimer::singleShot(3000, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
