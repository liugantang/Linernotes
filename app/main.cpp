#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include <core/Version.h>

int main(int argc, char *argv[])
{
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(aimusic::core::applicationName());
    QCoreApplication::setApplicationVersion(aimusic::core::versionString());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AI music player"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    QTextStream out(stdout);
    out << aimusic::core::applicationName() << u' ' << aimusic::core::versionString() << Qt::endl;

    return 0;
}
