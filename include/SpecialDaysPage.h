#pragma once
// ---------------------------------------------------------------------------
// SpecialDaysPage — birthdays, holidays, vacation starts: dates that
// matter on their own (addendum §3.14), added manually, sorted by
// whichever comes next.
//
// Same visual language as UpcomingPage on purpose — the two pages answer
// sibling questions ("what's due?" / "what's coming?"), and siblings
// should look related. Note the derived half even here: the LIST order
// and the "in N days" labels are computed from nextOccurrence() at every
// rebuild; only the day itself (title, date, repeats-yearly) is a fact.
// ---------------------------------------------------------------------------

#include <QWidget>

class AppData;
class QScrollArea;

class SpecialDaysPage : public QWidget
{
    Q_OBJECT

public:
    explicit SpecialDaysPage(AppData* data, QWidget* parent = nullptr);

public slots:
    void rebuild();

private:
    QWidget* buildContent();

    AppData*     m_data;
    QScrollArea* m_scroll = nullptr;
};
