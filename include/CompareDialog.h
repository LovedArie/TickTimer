#pragma once

#include "AppData.h"

#include <QDate>
#include <QDialog>
#include <QJsonObject>

class QLabel;

// ---------------------------------------------------------------------------
// CompareDialog — one day, two planners, side by side.
//
// Where the pieces come from is the whole story of this feature:
//   - MY numbers: stats::summarizeDay over the live AppData (borrowed,
//     const — comparing must not mutate).
//   - THEIR numbers: the peer's blob, fetched over the wire, poured into a
//     PRIVATE AppData via JsonStore::applyJsonObject — the same deserializer
//     the app has trusted since v1 — then through the very same
//     stats::summarizeDay. One summarizer, two datasets; the numbers are
//     comparable BECAUSE the code path is identical.
//
// The peer AppData is a value member owned by the dialog: a snapshot, born
// when the dialog opens, gone when it closes. It is wired to nothing —
// no store, no tracker, no signals — because it isn't "the app's data",
// it's a document we're reading. That distinction (live aggregate vs
// loaded snapshot) is the C++ lesson of this dialog.
//
// The date arrows re-summarize on every step — derive, don't store (§3.5),
// at dialog scale: no cached rows to go stale, just recompute (microseconds).
// ---------------------------------------------------------------------------

class CompareDialog : public QDialog
{
    Q_OBJECT
public:
    CompareDialog(const AppData* mine, const QString& peerName,
                  const QJsonObject& peerBlob, QWidget* parent = nullptr);

    // The refresh is public + parameterless so the UI test can drive the
    // dialog to a KNOWN date and read the labels — same reason MainWindow
    // grew showPage() for the screenshot tool.
    void showDay(const QDate& day);

private:
    void refresh();

    const AppData* m_mine;   // borrowed — the live planner
    AppData        m_peer;   // owned — a read-only snapshot of theirs
    QString        m_peerName;
    QDate          m_day;

    QLabel* m_dayLabel  = nullptr;
    QLabel* m_headline  = nullptr;
    // value cells [row][col]: rows focus/break/distracted/total,
    // cols me/them/delta. Filled by refresh().
    QLabel* m_cells[4][3] = {};
};
