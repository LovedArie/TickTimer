#pragma once
// ---------------------------------------------------------------------------
// QuickAddPreview — the parse readout as rich text, shared by every quick-add
// surface (the Activities input, the global capture overlay, and whatever
// comes next).
//
// Extracted in v21.1 the moment a SECOND consumer appeared — the same
// second-consumer rule that gave TaskRow (widgets, once) and TaskSnapshotModel
// (an algorithm) their homes. Two hand-rolled copies of "how do we show a
// parse?" would drift the first time one gains a chip the other doesn't; the
// preview IS the trust contract of quick-add, so it gets exactly one
// implementation.
//
// Header-only inline, like Task.h's label helpers: pure QString in/out, no
// widgets, no AppData — the CALLER resolves the category (a domain question)
// and passes the display outcome in. That keeps this usable from any layer
// and trivially testable if it ever grows logic worth pinning.
// ---------------------------------------------------------------------------

#include "QuickAddParser.h"

#include <QStringList>

// `chipText` empty = no category chip (the Activities input only shows one
// when a '#tag' was typed; the overlay always shows where the task will land).
// `chipResolved` false renders the chip grey with a trailing '?' — the "I
// didn't recognise that tag, falling back" signal.
inline QString quickAddPreviewHtml(const nlp::ParsedTask& p,
                                   const QString& chipText,
                                   bool chipResolved)
{
    QStringList bits;
    bits << (p.title.isEmpty()
                 ? QStringLiteral("<span style='color:#C25B54;'>%1</span>")
                       .arg(QObject::tr("(no title)"))
                 : QStringLiteral("<b style='color:#2B2F36;'>%1</b>")
                       .arg(p.title.toHtmlEscaped()));
    // The deadline chip: date, plus the clock when one was understood. The
    // preview IS quick-add's trust contract, so anything the parser claims
    // must be visible BEFORE Enter — a silently-added 17:00 would be worse
    // than no time parsing at all.
    QString when = p.dueDate.isValid() ? p.dueDate.toString("ddd MMM d")
                                       : QObject::tr("date TBD");
    if (p.dueTime.isValid())
        when += QStringLiteral(" ") + dueTimeLabel(p.dueTime);
    bits << QStringLiteral("<span style='color:#616974;'>%1</span>").arg(when);
    if (p.priority != Task::Priority::Medium)
        bits << QStringLiteral(
                    "<span style='color:%1; font-weight:700;'>%2</span>")
                    .arg(p.priority == Task::Priority::Urgent ? "#C25B54"
                                                              : "#8A93A0",
                         priorityLabel(p.priority).toUpper());
    if (p.repeat != Task::Repeat::None)
        bits << QStringLiteral("<span style='color:#616974;'>\u27F3 %1</span>")
                    .arg(repeatLabel(p.repeat));
    if (!chipText.isEmpty())
        bits << QStringLiteral("<span style='color:%1;'>#%2%3</span>")
                    .arg(chipResolved ? "#2F7E6E" : "#8A93A0",
                         chipText.toHtmlEscaped(),
                         chipResolved ? QString() : QStringLiteral("?"));
    return bits.join(
        QStringLiteral("<span style='color:#B9C0BA;'>  \u00B7  </span>"));
}
