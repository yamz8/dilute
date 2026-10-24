// dilute — everyday lab solution math (dilutions, molarity, serial dilutions,
// buffers) without mistakes, and a recipe card to take to the bench.

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "card.h"
#include "cli.h"
#include "portalfilepicker.h"
#include "recipemodel.h"
#include "spacekey.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // dilute themes itself; Qt's gtk3 platform theme only adds a
    // use-after-free inside GTK when the desktop theme changes, as Hype found.
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    // Commands draw no window, so they must not need a display, even where the
    // desktop exports QT_QPA_PLATFORM=wayland. Bare dilute prints help, as a
    // command line tool should; the launcher says dilute open.
    const bool command = argc == 1 || isCliCommand(argv[1]) || QByteArray(argv[1]) == "--version";
    if (command)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("dilute");
    app.setApplicationVersion("0.1.0");

    // Matches the window to dilute.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("dilute");
    app.setWindowIcon(QIcon::fromTheme("dilute"));

    if (app.arguments().value(1) == "--version") {
        fprintf(stdout, "dilute %s\n", qPrintable(app.applicationVersion()));
        return 0;
    }
    if (command)
        return runCli(app.arguments());

    QCommandLineParser args;
    args.setApplicationDescription("Lab solution math: dilutions, molarity, serial dilutions and buffers.\n\n" +
                                   cliSummary());
    args.addHelpOption();
    args.addPositionalArgument("recipe", ".dilute recipe to open", "[recipe]");
    QStringList arguments = app.arguments();
    if (arguments.value(1) == "open")
        arguments.removeAt(1);
    args.process(arguments);

    QQuickStyle::setStyle("Material");
    // A calculator is a dense form; the dense variant fits it on one screen.
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    PortalFilePicker picker;
    Backend backend(&picker);
    RecipeModel recipe(&backend);
    SpaceKey space;
    app.installEventFilter(&space);
    // A language switch changes number formats and translated notes, so the
    // recipe's amounts are worked out again.
    QObject::connect(&translator, &Translator::changed, &recipe, &RecipeModel::recompute);

    qmlRegisterType<CardView>("Dilute", 1, 0, "CardView");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("recipe", &recipe);
    engine.rootContext()->setContextProperty("spaceKey", &space);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    const QString file = args.positionalArguments().value(0);
    if (!file.isEmpty() && recipe.openPath(QUrl(file).isLocalFile() ? QUrl(file).toLocalFile() : file))
        engine.rootObjects().first()->setProperty("currentTab", 3);

    return app.exec();
}
