#pragma once

#include "live/liveitem.h"
#include "template/template.h"

#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QRectF>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class TemplateLibrary;

// Edits the library's templates for the template editor window (#94, plan 5.4): the list,
// one template's regions and safe area, and a preview of sample text through it. Edits
// apply at once and save on their own (plan 5.9: no Save button), at most every
// kSaveDelayMs so a drag or a slider doesn't write the library on every move. Saves go
// through TemplateLibrary, so what's live re-renders from them (#93). Built-in templates
// can't be edited, only duplicated.
class TemplateEditor : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    // {id, name, builtIn}: built-ins first, then by name.
    Q_PROPERTY(QVariantList templates READ templates NOTIFY changed)
    Q_PROPERTY(QString templateId READ templateId WRITE select NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool builtIn READ builtIn NOTIFY changed)
    // {id, name} per region of the template.
    Q_PROPERTY(QVariantList regions READ regions NOTIFY changed)
    Q_PROPERTY(QString regionId READ regionId WRITE selectRegion NOTIFY changed)
    // The selected region's style with the template JSON's keys (templatejson.h):
    // style.frame.x, style.shadow.opacity...
    Q_PROPERTY(QVariantMap style READ style NOTIFY changed)
    Q_PROPERTY(double safeAreaX READ safeAreaX NOTIFY changed)
    Q_PROPERTY(double safeAreaY READ safeAreaY NOTIFY changed)
    Q_PROPERTY(QString sampleText READ sampleText WRITE setSampleText NOTIFY changed)
    // The sample text through the template as edited, saved or not, as the output draws it.
    Q_PROPERTY(visualis::Content preview READ preview NOTIFY changed)
    // Why the last change was refused, each property once.
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    static constexpr int kSaveDelayMs = 150;

    explicit TemplateEditor(QObject *parent = nullptr);
    ~TemplateEditor() override;

    void classBegin() override {}
    void componentComplete() override;

    QVariantList templates() const;
    QString templateId() const;
    void select(const QString &id);
    QString name() const { return m_tmpl.name; }
    bool builtIn() const;
    QVariantList regions() const;
    QString regionId() const { return m_regionId; }
    void selectRegion(const QString &id);
    QVariantMap style() const;
    double safeAreaX() const { return m_tmpl.safeAreaX; }
    double safeAreaY() const { return m_tmpl.safeAreaY; }
    QString sampleText() const { return m_sampleText; }
    void setSampleText(const QString &text);
    visualis::Content preview() const;
    QStringList warnings() const { return m_warnings; }
    QString error() const { return m_error; }

    // A new template like the built-in Lyrics one, saved and selected. Returns its id, or
    // "" if it couldn't be saved.
    Q_INVOKABLE QString create(const QString &name);
    // A copy of the selected template, edits and all, saved and selected.
    Q_INVOKABLE QString duplicate();
    Q_INVOKABLE void rename(const QString &name);
    // Selects the built-in afterwards. Songs using it show with the built-in (#93).
    Q_INVOKABLE bool remove();

    // Sets properties of the selected region, keyed as in `style`; nested objects key by key.
    // If any value isn't valid (alone, or with the rest of the style), none applies and
    // `warnings` says why.
    Q_INVOKABLE void setStyle(const QVariantMap &properties);
    Q_INVOKABLE void setSafeArea(double x, double y);
    // The region's frame after dragging `edges` (Qt.LeftEdge | ..., all four to move it) by
    // (dx, dy) from `start`, the frame when the drag began: see draggedFrame.
    Q_INVOKABLE void dragFrame(const QRectF &start, int edges, double dx, double dy);

    // Saves an edit that's waiting to.
    Q_INVOKABLE void flush();

signals:
    void changed();

private:
    void follow();                 // the library's templates changed
    void load(const visualis::Template &tmpl);
    bool editable();               // sets `error` when not
    void edited();                 // an edit to save soon
    bool saveNow(visualis::Template &tmpl);
    visualis::TextRegion *region();

    QPointer<TemplateLibrary> m_library;
    visualis::Template m_tmpl;
    QString m_regionId;
    QString m_sampleText;
    QStringList m_warnings;
    QString m_error;
    bool m_pending = false;        // an edit waits to be saved
    QTimer m_save;                 // when it will be
};
