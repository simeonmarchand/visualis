#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>
#include <utility>

namespace visualis {

// Reads protobuf wire format without a schema: fields by number. Used for ProPresenter files,
// whose schema is undocumented (field numbers in propresenter.cpp). Every read is bounds-checked;
// malformed bytes give nullopt, never a crash.
class ProtoMessage
{
public:
    static std::optional<ProtoMessage> parse(const QByteArray &bytes);

    bool has(int field) const;
    // First value of a length-delimited field, as bytes / UTF-8 text / a nested message.
    QByteArray bytes(int field) const;
    QString string(int field) const;
    ProtoMessage message(int field) const;            // empty if missing or malformed
    // Every value of a repeated nested-message field; malformed entries are left out.
    QList<ProtoMessage> messages(int field) const;
    QList<QByteArray> allBytes(int field) const;
    quint64 varint(int field, quint64 fallback = 0) const;
    // A float (fixed32) or double (fixed64) field.
    std::optional<double> real(int field) const;

private:
    struct Value {
        int wireType;
        quint64 number;    // varint, or the raw fixed32/fixed64 bits
        QByteArray data;   // length-delimited
    };
    const Value *first(int field) const;

    QList<std::pair<int, Value>> m_fields;
};

} // namespace visualis
