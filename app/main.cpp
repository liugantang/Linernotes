#include <aimusic/core/Version.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(aimusic::core::applicationName());
  QCoreApplication::setApplicationVersion(aimusic::core::versionString());

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("AI music player"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.process(app);

  QTextStream out(stdout);
  out << aimusic::core::applicationName() << u' '
      << aimusic::core::versionString() << Qt::endl;

  return 0;
}
