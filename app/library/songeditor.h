#pragma once

#include "song/song.h"
#include "template/template.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
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
    // {id, label, color, slideCount, arranged, backgroundId}
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    // {id, name, isDefault}
    Q_PROPERTY(QVariantList arrangements READ arrangements NOTIFY changed)
    // The arrangement shown and edited; "" for the play order without one.
    Q_PROPERTY(QString arrangementId READ arrangementId WRITE selectArrangement NOTIFY changed)
    // {groupId, label, color} per step of that arrangement.
    Q_PROPERTY(QVariantList sequence READ sequence NOTIFY changed)
    // {key, text, groupLabel, groupColor, content, overrides} per slide, in that arrangement's
    // order. `key` names the slide (a group's repeats share it); `overrides` counts the
    // template properties it overrides.
    Q_PROPERTY(QVariantList previewSlides READ previewSlides NOTIFY changed)
    // Per-slide style (#95, plan 5.4): the slides selected, by key, and the region edited.
    // selectionChanged also follows every `changed`.
    Q_PROPERTY(QStringList selectedKeys READ selectedKeys NOTIFY selectionChanged)
    // {id, name} per region of the song's template.
    Q_PROPERTY(QVariantList regions READ regions NOTIFY changed)
    Q_PROPERTY(QString regionId READ regionId WRITE selectRegion NOTIFY selectionChanged)
    // The first selected slide's style in that region, as it shows, with the template JSON's
    // keys (as TemplateEditor::style).
    Q_PROPERTY(QVariantMap slideStyle READ slideStyle NOTIFY selectionChanged)
    // The paths ("color", "shadow.opacity") any selected slide overrides, and those whose
    // value differs between the selected slides.
    Q_PROPERTY(QStringList overriddenProperties READ overriddenProperties NOTIFY selectionChanged)
    Q_PROPERTY(QStringList mixedProperties READ mixedProperties NOTIFY selectionChanged)
    // The first selected slide's text transition (#96) as it shows, {kind, ms}; whether any
    // selected slide overrides it, and whether they differ.
    Q_PROPERTY(QVariantMap slideTransition READ slideTransition NOTIFY selectionChanged)
    Q_PROPERTY(bool slideTransitionOverridden READ slideTransitionOverridden NOTIFY selectionChanged)
    Q_PROPERTY(bool slideTransitionMixed READ slideTransitionMixed NOTIFY selectionChanged)
    Q_PROPERTY(int maxTransitionMs READ maxTransitionMs CONSTANT)
    // The template it shows with (#94); the built-in's id when it has none of its own.
    Q_PROPERTY(QString templateId READ templateId WRITE setTemplateId NOTIFY changed)
    // The song's default background (#99), "" for none. A group's, in `groups`, wins over it.
    Q_PROPERTY(QString backgroundId READ backgroundId WRITE setBackgroundId NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    static constexpr int kSaveDelayMs = 150;   // style edits save at most this often

    explicit SongEditor(QObject *parent = nullptr);
    ~SongEditor() override;

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
    QString templateId() const;
    void setTemplateId(const QString &id);
    QString backgroundId() const;
    void setBackgroundId(const QString &id);
    QStringList selectedKeys() const;
    QVariantList regions() const;
    QString regionId() const { return m_regionId; }
    void selectRegion(const QString &id);
    QVariantMap slideStyle() const;
    QStringList overriddenProperties() const;
    QStringList mixedProperties() const;
    QVariantMap slideTransition() const;
    bool slideTransitionOverridden() const;
    bool slideTransitionMixed() const;
    static int maxTransitionMs();
    QStringList warnings() const { return m_warnings; }
    QString error() const { return m_error; }

    Q_INVOKABLE void setTitle(const QString &title);
    Q_INVOKABLE void setCredits(const QString &ccliNumber, const QString &authors, const QString &copyright);
    Q_INVOKABLE void applyLyrics(const QString &text);

    Q_INVOKABLE void renameGroup(const QString &id, const QString &label);
    Q_INVOKABLE void setGroupColor(const QString &id, const QString &color);
    Q_INVOKABLE void setGroupBackground(const QString &id, const QString &assetId);
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

    // Selects the slide alone, or with `extend` adds it to the selection or takes it out.
    Q_INVOKABLE void selectSlide(const QString &key, bool extend);
    Q_INVOKABLE void clearSelection();
    // Overrides one property of the selected region on every selected slide. If the value
    // isn't valid (alone, or with the rest of a slide's style), no slide changes and
    // `warnings` says why. Style edits save on their own, at most every kSaveDelayMs.
    Q_INVOKABLE void setSlideProperty(const QString &path, const QVariant &value);
    // Back to the template's value, on every selected slide; refused like a set when that
    // would break a rule between properties.
    Q_INVOKABLE void resetSlideProperty(const QString &path);
    Q_INVOKABLE void resetSlides();
    // Overrides the text transition on every selected slide by `change`, {kind} or {ms} (as in
    // slideTransition). Only what it names changes, so across a mixed selection a new length
    // leaves each slide's kind. Refused like a property. Reset goes back to the template's.
    Q_INVOKABLE void setSlideTransition(const QVariantMap &change);
    Q_INVOKABLE void resetSlideTransition();
    // Saves a style edit that's waiting to. False if it couldn't be saved: a busy library keeps
    // the edit and is tried again; any other failure goes back to what's stored. After a
    // lyrics or other edit that didn't save, it waits for the next save that works.
    Q_INVOKABLE bool flush();

signals:
    void changed();
    void lyricsReset();
    void selectionChanged();

private:
    SongLibrary *library() const;
    visualis::Arrangement *shownArrangement(bool create);
    void save();
    visualis::Template songTemplate() const;
    // A slide by its group and place in it, so a group's repeats in the play order are one.
    struct SlideRef {
        QUuid group;
        int index = 0;
        bool operator==(const SlideRef &) const = default;
    };
    // The group holding the slide, or null if the slide is gone.
    const visualis::Group *groupOf(const SlideRef &slide) const;
    QList<QJsonObject> selectedOverrides() const;
    void pruneSelection();   // slides that are gone
    // Applies `edit` to every selected slide's override, or to none: refused when `edit`
    // returns a reason, or when a slide's style would break a rule between properties it
    // didn't break before.
    void editSelected(const std::function<QString(QJsonObject &)> &edit);
    void fitRegion();        // m_regionId to one the song's template has
    void styleEdited();
    void discardPendingStyle();

    visualis::Song m_song;
    QUuid m_arrangement;
    QStringList m_warnings;
    QString m_error;
    bool m_followsTemplates = false;   // connected to the library's templatesChanged
    QPointer<SongLibrary> m_library;   // for the last save, after the QML engine is gone
    QList<SlideRef> m_selected;
    QString m_regionId = QStringLiteral("main");
    bool m_stylePending = false;       // a style edit waits to be saved
    bool m_songUnsaved = false;        // the last whole-song save failed; m_song is ahead of the library
    QTimer m_styleSave;                // when it will be; backs off while the library is busy
};
