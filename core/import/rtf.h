#pragma once

#include <QByteArray>
#include <QString>

namespace visualis {

// Plain text from the Cocoa RTF that ProPresenter stores for slide text. Keeps the text,
// paragraph and line breaks, and \'hh and \uN escapes; drops font and color tables, pictures,
// and formatting. The spike version matched macOS's textutil on 300 of 300 real slides.
QString rtfToText(const QByteArray &rtf);

} // namespace visualis
