#pragma once

#include <QString>
#include <optional>

namespace visualis {

// A scripture reference found in some text, such as a Planning Center item title (P3
// finding 6). This only *recognizes* a reference; parsing it into verse ranges and looking
// the verses up is Phase 4's (spikes/scripture/refparse.py).
struct ScriptureMention {
    QString reference;   // as written, from the book name to the last number: "Acts 2:38"
    QString book;        // the book's full name: "Acts", "1 Corinthians"
    int start = -1;      // where `reference` starts in the text
};

// Finds the first scripture reference in `text`, or nothing. Books are the 66-book canon,
// by full name or a common abbreviation ("1 Cor", "Jn", "Ps").
//  - A text that is only a reference may name just chapters: "Psalm 23", "Jude 3".
//  - Inside longer text, only chapter:verse counts ("Scripture Reading: Acts 2:38"), and the
//    book must be written with at least three letters, so "Psalm 23 Choir" or "Service is
//    10:30" aren't taken for references. A time ("Mark 10:30 am") isn't one either.
std::optional<ScriptureMention> findScriptureReference(const QString &text);

} // namespace visualis
