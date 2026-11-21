#include "protobuf.h"

#include <cstring>

namespace visualis {

namespace {

bool readVarint(const QByteArray &buf, qsizetype &i, quint64 &out)
{
    out = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (i >= buf.size())
            return false;
        const auto b = quint8(buf[i++]);
        out |= quint64(b & 0x7F) << shift;
        if (!(b & 0x80))
            return true;
    }
    return false;   // more than 10 bytes: not a varint
}

} // namespace

std::optional<ProtoMessage> ProtoMessage::parse(const QByteArray &buf)
{
    ProtoMessage m;
    qsizetype i = 0;
    while (i < buf.size()) {
        quint64 key = 0;
        if (!readVarint(buf, i, key))
            return std::nullopt;
        const int field = int(key >> 3);
        const int wire = int(key & 7);
        if (field <= 0)
            return std::nullopt;
        Value v{wire, 0, {}};
        switch (wire) {
        case 0:
            if (!readVarint(buf, i, v.number))
                return std::nullopt;
            break;
        case 1:
        case 5: {
            const int size = wire == 1 ? 8 : 4;
            if (buf.size() - i < size)
                return std::nullopt;
            std::memcpy(&v.number, buf.constData() + i, size);   // little-endian, as on macOS/x86/ARM
            i += size;
            break;
        }
        case 2: {
            quint64 len = 0;
            if (!readVarint(buf, i, len) || len > quint64(buf.size() - i))
                return std::nullopt;
            v.data = buf.mid(i, qsizetype(len));
            i += qsizetype(len);
            break;
        }
        default:
            return std::nullopt;   // groups (3, 4) and unknown wire types aren't used by ProPresenter
        }
        m.m_fields.append({field, v});
    }
    return m;
}

const ProtoMessage::Value *ProtoMessage::first(int field) const
{
    for (const auto &[f, v] : m_fields) {
        if (f == field)
            return &v;
    }
    return nullptr;
}

bool ProtoMessage::has(int field) const
{
    return first(field) != nullptr;
}

QByteArray ProtoMessage::bytes(int field) const
{
    const Value *v = first(field);
    return v && v->wireType == 2 ? v->data : QByteArray();
}

QString ProtoMessage::string(int field) const
{
    return QString::fromUtf8(bytes(field));
}

ProtoMessage ProtoMessage::message(int field) const
{
    return parse(bytes(field)).value_or(ProtoMessage());
}

QList<ProtoMessage> ProtoMessage::messages(int field) const
{
    QList<ProtoMessage> out;
    for (const QByteArray &b : allBytes(field)) {
        if (auto m = parse(b))
            out << *m;
    }
    return out;
}

QList<QByteArray> ProtoMessage::allBytes(int field) const
{
    QList<QByteArray> out;
    for (const auto &[f, v] : m_fields) {
        if (f == field && v.wireType == 2)
            out << v.data;
    }
    return out;
}

quint64 ProtoMessage::varint(int field, quint64 fallback) const
{
    const Value *v = first(field);
    return v && v->wireType == 0 ? v->number : fallback;
}

std::optional<double> ProtoMessage::real(int field) const
{
    const Value *v = first(field);
    if (!v)
        return std::nullopt;
    if (v->wireType == 5) {
        float f;
        const quint32 bits = quint32(v->number);
        std::memcpy(&f, &bits, 4);
        return f;
    }
    if (v->wireType == 1) {
        double d;
        std::memcpy(&d, &v->number, 8);
        return d;
    }
    return std::nullopt;
}

} // namespace visualis
