#include "pco/songlink.h"

#include "song/labels.h"
#include "song/slidelist.h"
#include "song/songstore.h"

namespace visualis {

ServiceItem linkedItem(ServiceItem item, const Song &song)
{
    item.kind = ServiceItem::Kind::Song;
    item.ref = song.id;
    item.arrangement = QUuid();
    const QString pcoArrangement =
        item.importSnapshot.value(QStringLiteral("arrangement")).toObject().value(QStringLiteral("id")).toString();
    for (const Arrangement &a : song.arrangements) {
        if (!pcoArrangement.isEmpty() && a.pcoArrangementId == pcoArrangement)
            item.arrangement = a.id;
    }
    return item;
}

bool rememberPcoSong(Song &song, const PcoSong &pco)
{
    bool changed = false;
    const auto fill = [&](QString &field, const QString &value) {
        if (field.trimmed().isEmpty() && !value.trimmed().isEmpty()) {
            field = value.trimmed();
            changed = true;
        }
    };
    fill(song.pcoSongId, pco.id);
    fill(song.credits.ccliNumber, pco.ccliNumber);
    fill(song.credits.authors, pco.author);
    fill(song.credits.copyright, pco.copyright);
    return changed;
}

bool takePcoSong(Song &song, const PcoSong &pco)
{
    bool changed = false;
    if (!pco.id.isEmpty() && song.pcoSongId != pco.id) {
        song.pcoSongId = pco.id;
        changed = true;
    }
    return rememberPcoSong(song, pco) || changed;
}

bool releasePcoSong(Song &song, const QString &pcoSongId)
{
    if (pcoSongId.isEmpty() || song.pcoSongId != pcoSongId)
        return false;
    song.pcoSongId.clear();
    return true;
}

QStringList unmatchedLabels(const ServiceItem &item, const Song &song)
{
    QStringList out;
    for (const QString &label : matchSequence(item.customSequence, song.groups).unmatched) {
        if (!isLyricless(label))
            out << label;
    }
    return out;
}

QString SongLinkReport::summary() const
{
    const int matched = linked + byTitle;
    if (matched + toConfirm + placeholders == 0)
        return {};
    QStringList parts;
    QString m = matched == 1 ? QStringLiteral("1 song matched") : QStringLiteral("%1 songs matched").arg(matched);
    if (byTitle > 0)
        m += QStringLiteral(" (%1 by title)").arg(byTitle);
    parts << m;
    if (toConfirm > 0)
        parts << QStringLiteral("%1 to confirm").arg(toConfirm);
    if (placeholders > 0)
        parts << QStringLiteral("%1 not in the library").arg(placeholders);
    return parts.join(QStringLiteral(", ")) + QLatin1Char('.');
}

SongLinkReport linkSongs(Service &service, SongStore &songs)
{
    SongLinkReport report;
    QList<SongCandidate> candidates = songs.matchCandidates();
    for (ServiceItem &item : service.items) {
        if (item.kind != ServiceItem::Kind::Placeholder)
            continue;
        const std::optional<PcoSong> pco = snapshotSong(item.importSnapshot);
        if (!pco)
            continue;   // a plain item, not a song
        const SongMatch m = matchSong(*pco, candidates);
        if (m.kind == SongMatch::Kind::Confirm) {
            ++report.toConfirm;
            continue;
        }
        std::optional<Song> song = m.kind == SongMatch::Kind::Linked ? songs.load(m.songId) : std::nullopt;
        if (!song) {
            ++report.placeholders;
            continue;
        }
        if (rememberPcoSong(*song, *pco) && songs.save(*song).ok) {
            for (SongCandidate &c : candidates) {
                if (c.id == song->id)
                    c = SongCandidate{song->id, song->title, song->credits.ccliNumber, song->credits.authors, song->pcoSongId};
            }
        }
        item = linkedItem(item, *song);
        ++(m.by == SongMatch::By::Title ? report.byTitle : report.linked);
        if (const QStringList missing = unmatchedLabels(item, *song); !missing.isEmpty())
            report.unmatched << QStringLiteral("%1: %2").arg(song->title, missing.join(QStringLiteral(", ")));
    }
    return report;
}

} // namespace visualis
