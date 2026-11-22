#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>

namespace visualis {

class LiveState;

// Every operator action by name (plan 5.8). Keys, clicks, and later MIDI all run these, so
// there's one place to log them and, later, to switch external input off.
//
//   next_slide, prev_slide, goto_slide (arg: 0-based index),
//   goto_group (arg: a letter; see LiveState::goToGroupStartingWith),
//   goto_item (arg: the 0-based item index in the live service, or [item, slide] to land
//              on a slide other than the first, as a slide tile click does),
//   clear_text, clear_background, show_background, clear_all
//
// Returns false for an unknown name, a missing argument, or a command with nothing to do
// (the last slide's next_slide).
bool runCommand(LiveState &live, const QString &name, const QVariant &arg = {});

QStringList commandNames();

} // namespace visualis
