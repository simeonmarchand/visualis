#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstring>

// Builds ProPresenter-shaped protobuf bytes, so tests need no real (church, copyrighted) files.
namespace pb {

inline QByteArray varint(quint64 v)
{
    QByteArray out;
    do {
        quint8 b = v & 0x7F;
        v >>= 7;
        out += char(v ? b | 0x80 : b);
    } while (v);
    return out;
}
inline QByteArray bytes(int field, const QByteArray &data) { return varint(quint64(field) << 3 | 2) + varint(data.size()) + data; }
inline QByteArray str(int field, const QString &s) { return bytes(field, s.toUtf8()); }
inline QByteArray num(int field, quint64 v) { return varint(quint64(field) << 3) + v; }
inline QByteArray number(int field, quint64 v) { return varint(quint64(field) << 3) + varint(v); }
inline QByteArray f32(int field, float f)
{
    char raw[4];
    std::memcpy(raw, &f, 4);
    return varint(quint64(field) << 3 | 5) + QByteArray(raw, 4);
}
inline QByteArray uuid(int field, const QString &id) { return bytes(field, str(1, id)); }

inline QByteArray rtf(const QString &text)
{
    QString body;
    for (const QChar c : text) {
        if (c == '\n') body += "\\\n";
        else if (c.unicode() > 127) body += QStringLiteral("\\uc0\\u%1 ").arg(int(c.unicode()));
        else body += c;
    }
    return ("{\\rtf1\\ansi\\ansicpg1252\\cocoartf2761 {\\fonttbl\\f0\\fswiss Helvetica;}"
            "{\\colortbl;\\red255\\green255\\blue255;}\\pard\\qc\\f0\\fs120 \\cf1 " + body + "}").toUtf8();
}

// Cue { uuid=1, actions=10 { slide=23 { presentation=2 { base_slide=1 { elements=1 { element=1 { text=13 { rtf=5 }}}}}}}}
inline QByteArray cue(const QString &id, const QString &text)
{
    const QByteArray textMsg = bytes(5, rtf(text));
    const QByteArray element = bytes(1, bytes(13, textMsg));
    const QByteArray slide = bytes(1, element);
    const QByteArray action = bytes(23, bytes(2, bytes(1, slide)));
    return uuid(1, id) + bytes(10, action);
}

inline QByteArray color(float r, float g, float b, float a) { return f32(1, r) + f32(2, g) + f32(3, b) + f32(4, a); }

inline QByteArray cueGroup(const QString &id, const QString &name, const QByteArray &colorMsg, const QStringList &cueIds)
{
    QByteArray group = uuid(1, id) + str(2, name) + (colorMsg.isEmpty() ? QByteArray() : bytes(3, colorMsg));
    QByteArray out = bytes(1, group);
    for (const QString &c : cueIds)
        out += uuid(2, c);
    return out;
}

inline QByteArray arrangement(const QString &id, const QString &name, const QStringList &groupIds)
{
    QByteArray out = uuid(1, id) + str(2, name);
    for (const QString &g : groupIds)
        out += uuid(3, g);
    return out;
}

} // namespace pb
