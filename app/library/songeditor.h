#pragma once

#include "song/song.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class SongLibrary;

// Edits one song for the song editor window (#44, plan W3). Every change saves to the
// library at once (plan 5.9: no Save button). What's live isn't changed: the outputs keep
// the version that was taken live until it's taken live again, so an edit never moves the
// screen mid-song.
class SongEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString songId READ songId WRITE setSongId NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString ccliNumber READ ccliNumber NOTIFY changed)
    Q_PROPERTY(QString authors READ authors NOTIFY changed)
    Q_PROPERTY(QString copyright READ copyright NOTIFY changed)
    // The lyrics as text. Signalled only when the text should be replaced (load, a group
    // renamed from the list), not after each typed edit, so the cursor stays put.
    Q_PROPERTY(QString lyrics READ lyrics NOTIFY lyricsReset)
    // {id, label, color, slideCount, arranged}
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    // {id, name, isDefault}
    Q_PROPERTY(QVariantList arrangements READ arrangements NOTIFY changed)
    // The arrangement shown and edited; "" for the play order without one.
    Q_PROPERTY(QString arrangementId READ arrangementId WRITE selectArrangement NOTIFY changed)
    // {groupId, label, color} per step of that arrangement.
    Q_PROPERTY(QVariantList sequence READ sequence NOTIFY changed)
    // {text, groupLabel, groupColor} per slide, in that arrangement's order.
    Q_PROPERTY(QVariantList previewSlides READ previewSlides NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit SongEditor(QObject *parent = nullptr) : QObject(parent) {}

    QString songId() const;
    void setSongId(const QString &id);
    bool loaded() const { return !m_song.id.isNull(); }
    QString title() const { return m_song.title; }
    QString ccliNumber() const { return m_song.credits.ccliNumber; }
    QString authors() const { return m_song.credits.authors; }
    QString copyright() const { return m_song.credits.copyright; }
    QString lyrics() const;
    QVariantList groups() const;
    QVariantList arrangements() const;
    QString arrangementId() const;
    void selectArrangement(const QString &id);
    QVariantList sequence() const;
    QVariantList previewSlides() const;
    QStringList warnings() const { return m_warnings; }
    QString error() const { return m_error; }

    Q_INVOKABLE void setTitle(const QString &title);
    Q_INVOKABLE void setCredits(const QString &ccliNumber, const QString &authors, const QString &copyright);
    Q_INVOKABLE void applyLyrics(const QString &text);

    Q_INVOKABLE void renameGroup(const QString &id, const QString &label);
    Q_INVOKABLE void setGroupColor(const QString &id, const QString &color);
    Q_INVOKABLE void removeGroup(const QString &id);

    // Returns the new arrangement's id.
    Q_INVOKABLE QString addArrangement(const QString &name);
    Q_INVOKABLE void renameArrangement(const QString &id, const QString &name);
    Q_INVOKABLE void removeArrangement(const QString &id);
    Q_INVOKABLE void makeDefaultArrangement(const QString &id);
    // Steps of the shown arrangement. With no arrangement yet, the first change makes a
    // "Default" one from the groups in order.
    Q_INVOKABLE void addStep(const QString &groupId);
    Q_INVOKABLE void removeStep(int index);
    Q_INVOKABLE void moveStep(int index, int delta);

    Q_INVOKABLE bool deleteSong();

signals:
    void changed();
    void lyricsReset();

private:
    SongLibrary *library() const;
    visualis::Arrangement *shownArrangement(bool create);
    void save();

    visualis::Song m_song;
    QUuid m_arrangement;
    QStringList m_warnings;
    QString m_error;
};
