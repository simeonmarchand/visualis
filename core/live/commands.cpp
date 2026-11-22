#include "live/commands.h"

#include "live/livestate.h"

namespace visualis {

bool runCommand(LiveState &live, const QString &name, const QVariant &arg)
{
    if (name == "next_slide")
        return live.next();
    if (name == "prev_slide")
        return live.previous();
    if (name == "goto_slide") {
        bool ok = false;
        const int n = arg.toInt(&ok);
        return ok && live.goToSlide(n);
    }
    if (name == "goto_item") {
        const QVariantList parts = arg.typeId() == QMetaType::QVariantList ? arg.toList() : QVariantList{arg};
        bool itemOk = false, slideOk = true;
        const int item = parts.value(0).toInt(&itemOk);
        const int slide = parts.size() > 1 ? parts[1].toInt(&slideOk) : 0;
        return itemOk && slideOk && parts.size() <= 2 && live.goToItem(item, slide);
    }
    if (name == "goto_group")
        return live.goToGroupStartingWith(arg.toString().left(1));
    if (name == "clear_text") {
        live.clearText();
        return true;
    }
    if (name == "clear_background") {
        live.clearBackground();
        return true;
    }
    if (name == "show_background") {
        live.showBackground();
        return true;
    }
    if (name == "clear_all") {
        live.clearAll();
        return true;
    }
    return false;
}

QStringList commandNames()
{
    return {"next_slide", "prev_slide", "goto_slide", "goto_group", "goto_item",
            "clear_text", "clear_background", "show_background", "clear_all"};
}

} // namespace visualis
