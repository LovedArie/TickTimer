#pragma once
// ---------------------------------------------------------------------------
// SpecialDay — a date that matters on its own: a birthday, a holiday, the
// first day of a vacation (addendum §3.14). Standalone: it references
// nothing and nothing references it.
//
// The interesting design is what is NOT stored: the next occurrence of a
// yearly day is DERIVED by nextOccurrence(), never saved — a stored "next"
// would be wrong every January 1st (derive, don't store, §3.5, again).
// ---------------------------------------------------------------------------

#include <QColor>
#include <QDate>
#include <QString>

struct SpecialDay
{
    QString id;
    QString title;                // "Maman's birthday", "Christmas"
    QDate   date;                 // the (first) occurrence
    bool    repeatsYearly = false;

    // v7: a chosen colour. Invalid (the default) means "no choice made" —
    // the card keeps colouring itself by urgency, exactly as before. A
    // valid colour is the owner saying "this day is THIS colour"; the card
    // honours it. Absence-as-default again: old files change nothing.
    QColor color;

    // The next time this day happens, seen from `today`. Today itself
    // counts as upcoming (your birthday is not "passed" at breakfast).
    //
    // THE EDGE CASE, decided at design time instead of discovered in
    // production: a Feb 29 anniversary in a common year resolves to
    // Mar 1. Arbitrary — Feb 28 would be equally fine — but a program
    // needs ONE written-down answer, and now it has it (addendum §3.14).
    QDate nextOccurrence(QDate today) const
    {
        if (!repeatsYearly)
            return date;

        QDate next(today.year(), date.month(), date.day());
        if (!next.isValid())                      // Feb 29, common year
            next = QDate(today.year(), 3, 1);
        if (next < today) {
            next = QDate(today.year() + 1, date.month(), date.day());
            if (!next.isValid())
                next = QDate(today.year() + 1, 3, 1);
        }
        return next;
    }
};
