#include "AppController.h"
#include "MainWindow.h"
#include "MapCanvas.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QLocale>
#include <QStyleHints>
#include <QTimer>
#include <QTranslator>

#include <oclero/qlementine.hpp>

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

void applyQlementineTheme(
    oclero::qlementine::QlementineStyle *style, const QString &theme)
{
    const bool followsSystem = theme == QStringLiteral("System");
    const bool dark = theme == QStringLiteral("Dark")
        || (followsSystem
            && QApplication::styleHints()->colorScheme()
                == Qt::ColorScheme::Dark);
    style->setThemeJsonPath(
        dark ? QStringLiteral(":/themes/qlementine-dark.json")
             : QStringLiteral(":/themes/qlementine-light.json"));
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

    auto *qlementineStyle =
        new oclero::qlementine::QlementineStyle(&application);
    qlementineStyle->setAutoIconColor(
        oclero::qlementine::AutoIconColor::TextColor);
    QApplication::setStyle(qlementineStyle);
    applyQlementineTheme(qlementineStyle,
                         AppController::savedOrPlatformStyle());
    QTranslator translator;
    applyLanguage(application, translator,
                  AppController::savedOrSystemLanguage());
    configureGdalData();
    registerMapnikInputPlugins();
    GDALAllRegister();

    AppController controller;
    QFont font = application.font();
    font.setFamily(controller.fontFamily());
    font.setPointSize(controller.fontSize());
    application.setFont(font);

    MainWindow window(&controller);
    const auto refreshTheme = [&application, &controller, qlementineStyle] {
        applyQlementineTheme(qlementineStyle, controller.qtStyle());
        QFont font = application.font();
        font.setFamily(controller.fontFamily());
        font.setPointSize(controller.fontSize());
        application.setFont(font);
    };
    QObject::connect(&controller, &AppController::qtStyleChanged,
                     &window, refreshTheme);
    QObject::connect(
        application.styleHints(), &QStyleHints::colorSchemeChanged,
        &window, [&controller, refreshTheme](Qt::ColorScheme) {
            if (controller.qtStyle() == QStringLiteral("System"))
                refreshTheme();
        });
    QObject::connect(
        &controller, &AppController::languageChanged, &window,
        [&application, &translator, &controller] {
            applyLanguage(application, translator, controller.language());
        });
    if (parser.isSet(panelOption))
        window.setActivePanel(parser.value(panelOption));
    window.show();
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
                    [&window, row, longitude, latitude] {
                    window.setActivePanel(QStringLiteral("vector"));
                    window.selectVectorFeature(row, longitude, latitude);
                });
            }
        }
    }
    const auto invokeLayerDialog =
        [&parser, &window](const QCommandLineOption &option,
                          bool metadata) {
            if (!parser.isSet(option))
                return;
            bool rowOk = false;
            const int row = parser.value(option).toInt(&rowOk);
            if (!rowOk)
                return;
            QTimer::singleShot(600, &window, [&window, row, metadata] {
                if (metadata)
                    window.showMetadata(row);
                else
                    window.showAttributeTable(row);
            });
        };
    invokeLayerDialog(metadataRowOption, true);
    invokeLayerDialog(attributeTableRowOption, false);
    if (parser.isSet(screenshotOption)) {
        const QString outputPath =
            QFileInfo(parser.value(screenshotOption)).absoluteFilePath();
        QTimer::singleShot(2500, &application,
                           [&application, &window, outputPath] {
            QWidget *target = QApplication::activeWindow();
            if (!target)
                target = &window;
            target->grab().save(outputPath);
            application.quit();
        });
    } else if (parser.isSet(smokeTestOption)) {
        QTimer::singleShot(3000, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
