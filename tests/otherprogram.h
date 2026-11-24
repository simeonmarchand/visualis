#pragma once

#include <QDir>
#include <QSqlDatabase>
#include <QUuid>

// Another program (sqlite3, a backup tool) with its own connection to a library's database.
// The library lock (#109) keeps out other Visualis processes, not these, so code that copes
// with a database someone else has locked is tested against one of these.
class OtherProgram
{
public:
    explicit OtherProgram(const QString &libraryFolder)
        : m_name(QStringLiteral("other-program-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_name);
        db.setDatabaseName(QDir(libraryFolder).filePath("library.sqlite"));
        db.open();
    }
    ~OtherProgram()
    {
        QSqlDatabase::database(m_name, false).close();
        QSqlDatabase::removeDatabase(m_name);
    }
    OtherProgram(const OtherProgram &) = delete;
    OtherProgram &operator=(const OtherProgram &) = delete;

    QSqlDatabase database() const { return QSqlDatabase::database(m_name, false); }

private:
    QString m_name;
};
