#include "CatchUpCard.h"

#include "AppData.h"
#include "DayBriefing.h"  // brief::clockLabel / spanLabel — the app's ONE
                          // spelling of "09:30" and "1h 05m"
#include "MissedBlocks.h"
#include "Prefs.h"
#include "SlidePanel.h"
#include "TaskCoverage.h" // coverage::deadlineOf — the block's task bounds
                          // the proposer's search

#include <QHBoxLayout>
#include <QSizePolicy>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace
{
QLabel* softLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName("sub");
    l->setWordWrap(true);
    return l;
}

// "Mon 09:00–10:30" — the weekday is the useful coordinate this close in.
QString whenLabel(const Event& e)
{
    return QStringLiteral("%1 %2–%3")
        .arg(QLocale::system().dayName(e.date.dayOfWeek(),
                                       QLocale::ShortFormat),
             brief::clockLabel(e.plannedStartMinutes),
             brief::clockLabel(e.plannedEndMinutes));
}

// The verdict in words the numbers back up. Never "you failed": the
// non-shaming rule applies hardest to the surface that exists only to
// discuss what went wrong.
QString verdictLabel(const missed::Verdict& v)
{
    if (v.reason == missed::Reason::NeverStarted)
        return QObject::tr("never started");
    return QObject::tr("got %1 of %2")
        .arg(brief::spanLabel(v.focusSeconds),
             brief::spanLabel(v.plannedSeconds));
}

QString optionLabel(const reschedule::Option& o, QDate today)
{
    const auto dayWord = [today](QDate d) {
        if (d == today)
            return QObject::tr("today");
        if (d == today.addDays(1))
            return QObject::tr("tomorrow");
        return QLocale::system().dayName(d.dayOfWeek(), QLocale::ShortFormat);
    };

    switch (o.kind) {
    case reschedule::Kind::FreeSlot:
    case reschedule::Kind::BeyondDeadline: {
        const reschedule::Piece& p = o.pieces.first();
        const QString base = QObject::tr("%1 %2")
                                 .arg(dayWord(p.date),
                                      brief::clockLabel(p.startMinutes));
        return o.kind == reschedule::Kind::BeyondDeadline
                   ? QObject::tr("%1 — past the deadline").arg(base)
                   : base;
    }
    case reschedule::Kind::Split:
        return QObject::tr("split into %n part(s)", nullptr,
                           int(o.pieces.size()));
    case reschedule::Kind::Shorten:
        return QObject::tr("%1 %2, only %3")
            .arg(dayWord(o.pieces.first().date),
                 brief::clockLabel(o.pieces.first().startMinutes),
                 brief::spanLabel(o.recoveredSeconds));
    case reschedule::Kind::Bump:
        return {}; // not offered as a tap — §K's scope note still holds
    }
    return {};
}

// The chip's two coats. Styled inline (not Theme.h) like the sibling card's
// rows: these strings are the chip's STATE made visible, and state lives
// with the widget that derives it.
// Two coats, one geometry. Radius is 14px, NOT the CSS pill trick of
// 999px: Qt's stylesheet engine silently DROPS a border-radius larger than
// half the widget's height, so 999px renders sharp-cornered boxes — the
// owner's screenshot caught exactly that. 14px sits safely under half of
// the ~30px pill height. (v26.7.2; the same fix applies to the sibling's
// strips.)
//
// The muted coat is FADED SOLID, not dashed (v26.7.2). A 1px dashed border
// is high-frequency edge detail — dozens of contrast flips the eye keeps
// re-processing; the owner reported literal eye fatigue. "Waiting" is
// carried by weight and contrast (light border, gray text, no fill), not by
// line style: the rule is strong = actionable, faded = waiting.
const char* kProminentChip =
    "QPushButton#catchUpChip { background:#FBF4E8; border:1px solid #EAD9BC;"
    " border-radius:14px; padding:5px 12px; font-weight:600; color:#1E2228;"
    " text-align:left; }"
    "QPushButton#catchUpChip:hover { background:#F6EAD3; }";
const char* kMutedChip =
    "QPushButton#catchUpChip { background:transparent;"
    " border:1px solid #E0E2E6; border-radius:14px; padding:5px 12px;"
    " font-weight:500; color:#767E89; text-align:left; }"
    "QPushButton#catchUpChip:hover { color:#4F5560; border-color:#C9CDD3; }";
} // namespace

CatchUpCard::CatchUpCard(const AppData* data, QWidget* parent)
    : QFrame(parent)
    , m_data(data)
{
    setObjectName("catchUpCard");

    // The whole card is one chip in a left-packed row. Built ONCE: refresh
    // restyles and relabels it in place. A widget that is never destroyed
    // can never be destroyed under a click — the v22.2 bug class doesn't
    // get mitigated here, it gets made impossible.
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    m_chip = new QPushButton(this);
    m_chip->setObjectName("catchUpChip");
    m_chip->setCursor(Qt::PointingHandCursor);
    // The radius clamp's SECOND bite (v26.7.3): 14px is only honoured if
    // the button is at least 28px tall, and font metrics decide the height
    // — on Windows' default 9pt the pill lands near 25px, half is 12.5,
    // and Qt silently drops the radius AGAIN. A legal radius needs a
    // guaranteed height, so the height is pinned rather than the radius
    // shrunk: 30px makes 14px lawful on every machine.
    m_chip->setFixedHeight(30);
    row->addWidget(m_chip);
    // No trailing stretch since v26.7.1: the card sits INSIDE the panel's
    // shared review row now, and the row owns the leftover space. A stretch
    // here would fight the row's and win half the time — one layout decides
    // the slack, never two.
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    connect(m_chip, &QPushButton::clicked, this, [this]() {
        // Opening is ALWAYS allowed — muted included. The snooze governs
        // attention (chip intensity), never access: the midday
        // "finished early, let me review" tap is the workflow this whole
        // redesign exists to serve, and it must cost exactly one click.
        //
        // m_lastNow, not the wall clock: the ONE persistent handler must
        // honour the nowProvider doctrine like every rebuilt lambda does,
        // or the tests' injected time and the chip's would disagree.
        fillDrawer(m_lastNow);
        drawer()->open();
    });

    hide();
}

void CatchUpCard::setSuppressed(bool suppressed)
{
    if (m_suppressed == suppressed)
        return;
    m_suppressed = suppressed;
    m_lastPrint.clear(); // force the next refresh to re-decide visibility
}

bool CatchUpCard::isEveningAt(const QDateTime& now) const
{
    const int dayEnd = prefs::agendaWindow().second;
    const int nowMin = now.time().hour() * 60 + now.time().minute();
    return nowMin >= dayEnd - 90;
}

QDateTime CatchUpCard::snoozeTarget() const
{
    const QDateTime stored =
        QSettings().value(QStringLiteral("catchup/snoozedUntil")).toDateTime();
    if (m_sessionSnooze.isValid() && stored.isValid())
        return qMax(m_sessionSnooze, stored);
    return m_sessionSnooze.isValid() ? m_sessionSnooze : stored;
}

bool CatchUpCard::snoozedAt(const QDateTime& now) const
{
    const QDateTime until = snoozeTarget();
    return until.isValid() && until > now;
}

QString CatchUpCard::fingerprint(const QDateTime& now) const
{
    QString print;
    print += now.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    print += QLatin1Char('|');
    print += isEveningAt(now) ? QLatin1Char('E') : QLatin1Char('M');
    print += snoozedAt(now)
                 ? QLatin1Char('S') + snoozeTarget().toString(
                       QStringLiteral("MMdd-HHmm"))
                 : QString(QLatin1Char('-'));
    print += m_expanded;
    print += m_showResolved ? QLatin1Char('R') : QLatin1Char('-');
    print += QString::number(m_undoIds.size()) + m_undoLabel;
    // The drawer's openness is a rendering input: the same data paints
    // differently depending on whether the drawer needs refilling.
    print += (m_drawer && m_drawer->isOpen()) ? QLatin1Char('D')
                                              : QLatin1Char('-');
    print += QLatin1Char('|');

    const missed::Rule rule = prefs::missedRule();
    print += QString::number(rule.minPercent) + QLatin1Char(',')
             + QString::number(rule.lookBackDays);

    for (const Event* e : missed::unresolvedIn(m_data->events(), rule, now)) {
        const missed::Verdict v = missed::judge(*e, rule, now);
        print += QLatin1Char('|') + e->id + QLatin1Char(':')
                 + QString::number(v.focusSeconds) + QLatin1Char('/')
                 + QString::number(v.plannedSeconds);
    }
    for (const Event* e :
         missed::recentlyResolvedIn(m_data->events(), rule, now))
        print += QLatin1Char('|') + e->id + QLatin1Char('#')
                 + blockOutcomeToString(e->outcome);
    return print;
}

void CatchUpCard::refresh(const QDateTime& now)
{
    m_lastNow = now; // the persistent chip handler's clock (see ctor)
    const QString print = fingerprint(now);
    if (print == m_lastPrint)
        return;
    m_lastPrint = print;

    applyChip(now);
    if (m_drawer && m_drawer->isOpen())
        fillDrawer(now); // every data change that reaches the card refreshes
                         // the open drawer for free — the sibling's pipeline
}

void CatchUpCard::applyChip(const QDateTime& now)
{
    const missed::Rule rule = prefs::missedRule();
    const int unresolvedN =
        missed::unresolvedIn(m_data->events(), rule, now).size();
    const int resolvedN =
        missed::recentlyResolvedIn(m_data->events(), rule, now).size();

    m_hasAnything = unresolvedN > 0;

    if (m_suppressed || (unresolvedN == 0 && resolvedN == 0)) {
        hide(); // the gate's veto, or ABSENT: zero rent either way
        return;
    }
    show();

    const bool evening = isEveningAt(now);
    const bool momentOn =
        evening ? prefs::catchUpAtEndOfDay() : prefs::catchUpOnOpen();

    if (unresolvedN > 0 && momentOn && !snoozedAt(now)) {
        // PROMINENT — the feature's stage time.
        m_chip->setStyleSheet(QString::fromLatin1(kProminentChip));
        m_chip->setText(tr("%n to catch up", nullptr, unresolvedN));
        m_chip->setToolTip(
            evening ? tr("Blocks from the last days that didn't happen — "
                         "decide before the day closes.")
                    : tr("Blocks that didn't happen. One tap to review."));
        return;
    }

    // MUTED — real, but not now. The label names WHY it is quiet: a snooze
    // says when it comes back; a silenced moment just shows the count.
    m_chip->setStyleSheet(QString::fromLatin1(kMutedChip));
    if (unresolvedN > 0 && snoozedAt(now)) {
        const QDateTime until = snoozeTarget();
        m_chip->setText(
            tr("%n · back %1", nullptr, unresolvedN)
                .arg(until.date() == now.date()
                         ? until.time().toString(QStringLiteral("HH:mm"))
                         : tr("tomorrow")));
        m_chip->setToolTip(tr("Snoozed — tap to review anyway."));
    } else if (unresolvedN > 0) {
        m_chip->setText(tr("%n to catch up", nullptr, unresolvedN));
        m_chip->setToolTip(tr("Tap to review."));
    } else {
        m_chip->setText(tr("%n resolved", nullptr, resolvedN));
        m_chip->setToolTip(tr("Recently decided blocks — tap to review or "
                              "bring one back."));
    }
}

SlidePanel* CatchUpCard::drawer()
{
    if (!m_drawer) {
        QWidget* host = m_drawerHost ? m_drawerHost
                                     : (parentWidget() ? parentWidget()
                                                       : this);
        m_drawer = new SlidePanel(host);
        connect(m_drawer, &SlidePanel::closed, this, [this]() {
            // Folding state dies with the drawer: reopening starts calm.
            m_expanded.clear();
            m_showResolved = false;
        });
    }
    return m_drawer;
}

void CatchUpCard::fillDrawer(const QDateTime& now)
{
    SlidePanel* panel = drawer();
    panel->clearContent();
    QVBoxLayout* v = panel->contentLayout();
    const auto put = [v](QWidget* w) { v->insertWidget(v->count() - 1, w); };
    const auto putLay = [v](QLayout* l) {
        v->insertLayout(v->count() - 1, l);
    };

    const missed::Rule rule = prefs::missedRule();
    const QVector<const Event*> unresolved =
        missed::unresolvedIn(m_data->events(), rule, now);
    const QVector<const Event*> resolved =
        missed::recentlyResolvedIn(m_data->events(), rule, now);

    panel->setTitle(unresolved.isEmpty()
                        ? tr("Catch up")
                        : tr("Catch up · %n block(s)", nullptr,
                             int(unresolved.size())));

    // ---- the receipt (§K.2), first thing under the title -------------------
    if (!m_undoIds.isEmpty()) {
        auto* receipt = new QHBoxLayout;
        receipt->setSpacing(6);
        receipt->addWidget(softLabel(m_undoLabel, panel), 1);
        auto* undo = new QPushButton(tr("Undo"), panel);
        undo->setObjectName("catchUpUndo");
        undo->setCursor(Qt::PointingHandCursor);
        undo->setFlat(true);
        const QStringList ids = m_undoIds; // copy before clearing — the
                                           // lambda must not read members
                                           // it is about to reset
        connect(undo, &QPushButton::clicked, this, [this, ids, now]() {
            m_undoIds.clear();
            m_undoLabel.clear();
            emit resolveAllRequested(ids, BlockOutcome::Unset);
            refresh(now);
        });
        receipt->addWidget(undo);
        putLay(receipt);
    }

    // ---- the rows: ALL of them ---------------------------------------------
    // The old kMaxRows cap was the fixed-320px panel defending itself; the
    // drawer scrolls, so the cap retires with the crowding it existed for.
    for (const Event* e : unresolved)
        put(makeRow(*e, now));

    // ---- the resolved section (§K.3, relocated) -----------------------------
    if (!resolved.isEmpty()) {
        auto* toggle = new QPushButton(
            m_showResolved
                ? tr("Hide resolved")
                : tr("%n resolved · bring back", nullptr, int(resolved.size())),
            panel);
        toggle->setObjectName("catchUpResolvedChip"); // name kept: same handle,
                                                      // new address
        toggle->setCursor(Qt::PointingHandCursor);
        toggle->setFlat(true);
        connect(toggle, &QPushButton::clicked, this, [this, now]() {
            m_showResolved = !m_showResolved;
            m_lastPrint.clear(); // the flag is in the print; force the refill
            refresh(now);
        });
        put(toggle);

        if (m_showResolved) {
            for (const Event* e : resolved) {
                auto* line = new QHBoxLayout;
                line->setSpacing(6);
                line->addWidget(
                    softLabel(QStringLiteral("%1 · %2 — %3")
                                  .arg(m_data->eventLabel(*e), whenLabel(*e),
                                       e->outcome == BlockOutcome::Done
                                           ? tr("done")
                                           : tr("skipped")),
                              panel),
                    1);
                auto* back = new QPushButton(tr("Bring back"), panel);
                back->setObjectName("catchUpBringBack");
                back->setCursor(Qt::PointingHandCursor);
                back->setFlat(true);
                const QString id = e->id;
                connect(back, &QPushButton::clicked, this, [this, id]() {
                    emit resolveRequested(id, BlockOutcome::Unset);
                });
                line->addWidget(back);
                putLay(line);
            }
            if (resolved.size() > 1) {
                QStringList ids;
                ids.reserve(resolved.size());
                for (const Event* e : resolved)
                    ids.append(e->id);
                auto* all = new QPushButton(
                    tr("Bring all %n back", nullptr, int(resolved.size())),
                    panel);
                all->setObjectName("catchUpBringAllBack");
                all->setCursor(Qt::PointingHandCursor);
                all->setFlat(true);
                connect(all, &QPushButton::clicked, this, [this, ids]() {
                    emit resolveAllRequested(ids, BlockOutcome::Unset);
                });
                put(all);
            }
        }
    }

    // ---- the footer verbs ---------------------------------------------------
    if (!unresolved.isEmpty()) {
        auto* footer = new QHBoxLayout;
        footer->setSpacing(6);
        if (unresolved.size() > 1) {
            QStringList ids;
            ids.reserve(unresolved.size());
            for (const Event* e : unresolved)
                ids.append(e->id);
            auto* skipAll = new QPushButton(
                tr("Skip all %n", nullptr, int(unresolved.size())), panel);
            skipAll->setObjectName("catchUpSkipAll");
            skipAll->setToolTip(tr("Mark every listed block as deliberately "
                                   "skipped. Nothing is deleted — Undo "
                                   "appears right here."));
            skipAll->setCursor(Qt::PointingHandCursor);
            skipAll->setFlat(true);
            connect(skipAll, &QPushButton::clicked, this, [this, ids, now]() {
                m_undoIds   = ids; // the receipt, written BEFORE the deed
                m_undoLabel =
                    tr("Skipped %n block(s).", nullptr, int(ids.size()));
                emit resolveAllRequested(ids, BlockOutcome::Dropped);
                refresh(now); // tests drive this path without the changed()
                              // pipeline; in the app the refresh is a no-op
                              // repeat of the one changed() already caused
            });
            footer->addWidget(skipAll);
        }
        footer->addStretch(1);
        auto* later = new QPushButton(tr("Later"), panel);
        later->setObjectName("catchUpLater");
        later->setToolTip(tr("Quiet the chip until the evening. It stays "
                             "one tap away the whole time."));
        later->setCursor(Qt::PointingHandCursor);
        later->setFlat(true);
        connect(later, &QPushButton::clicked, this, [this, now]() {
            const bool evening = isEveningAt(now);
            const int dayEnd = prefs::agendaWindow().second;
            const QDateTime until =
                evening
                    ? QDateTime(now.date().addDays(1), QTime(0, 0))
                    : QDateTime(now.date(), QTime((dayEnd - 90) / 60,
                                                  (dayEnd - 90) % 60));
            m_sessionSnooze = until;
            QSettings().setValue(QStringLiteral("catchup/snoozedUntil"),
                                 until);
            m_undoIds.clear(); // a snooze is a deliberate filing-away,
            m_undoLabel.clear(); // receipt included (§K.2's boundary)
            drawer()->closePanel();
            refresh(now); // demote the chip to muted
        });
        footer->addWidget(later);
        putLay(footer);
    }
}

QWidget* CatchUpCard::makeRow(const Event& block, const QDateTime& now)
{
    SlidePanel* panel = drawer();
    auto* row = new QWidget(panel);
    auto* v = new QVBoxLayout(row);
    v->setContentsMargins(0, 4, 0, 8);
    v->setSpacing(4);

    const missed::Rule rule = prefs::missedRule();
    const missed::Verdict verdict = missed::judge(block, rule, now);

    auto* what = new QLabel(QStringLiteral("%1 · %2 — %3")
                                .arg(m_data->eventLabel(block),
                                     whenLabel(block),
                                     verdictLabel(verdict)),
                            row);
    what->setWordWrap(true);
    v->addWidget(what);

    reschedule::Context ctx;
    ctx.now = now;
    if (const Task* t = m_data->taskById(block.taskId))
        ctx.deadline = coverage::deadlineOf(*t, now.date());
    const auto window = prefs::agendaWindow();
    ctx.dayStartMinutes = window.first;
    ctx.dayEndMinutes   = window.second;
    ctx.horizonDays     = prefs::catchUpHorizonDays();

    const QVector<reschedule::Option> options =
        reschedule::propose(block, verdict, m_data->events(), ctx);

    QVector<reschedule::Option> placements;
    QVector<reschedule::Option> bumps;
    for (const reschedule::Option& o : options)
        (o.kind == reschedule::Kind::Bump ? bumps : placements).append(o);

    // v26.3's layout rule survives the move into the drawer: the primary
    // action owns a full-width row; secondary actions are one short word
    // each. The drawer is wider than the 320px panel was, but "at most one
    // variable-width label per button row" is a rule, not a workaround.
    const QString id = block.id;

    if (!placements.isEmpty()) {
        const reschedule::Option top = placements.first();
        auto* move = new QPushButton(
            tr("Move → %1").arg(optionLabel(top, now.date())), row);
        move->setObjectName("catchUpMove");
        move->setCursor(Qt::PointingHandCursor);
        connect(move, &QPushButton::clicked, this, [this, id, top]() {
            emit acceptProposalRequested(id, top);
        });
        v->addWidget(move);

        if (m_expanded == id && placements.size() > 1) {
            for (int i = 1; i < placements.size(); ++i) {
                const reschedule::Option o = placements.at(i);
                auto* alt = new QPushButton(
                    QStringLiteral("· %1").arg(optionLabel(o, now.date())),
                    row);
                alt->setObjectName("catchUpAlt");
                alt->setCursor(Qt::PointingHandCursor);
                alt->setFlat(true);
                connect(alt, &QPushButton::clicked, this, [this, id, o]() {
                    emit acceptProposalRequested(id, o);
                });
                v->addWidget(alt);
            }
        }
    } else if (!bumps.isEmpty()) {
        const Event* victim = m_data->eventById(bumps.first().bumpEventId);
        const QDate day = bumps.first().pieces.first().date;
        v->addWidget(softLabel(
            victim ? tr("Nothing free before the deadline — in the way: "
                        "%1, %2")
                         .arg(m_data->eventLabel(*victim), whenLabel(*victim))
                   : tr("Nothing free before the deadline."),
            row));
        auto* open = new QPushButton(tr("Open that day"), row);
        open->setObjectName("catchUpOpenDay");
        open->setCursor(Qt::PointingHandCursor);
        connect(open, &QPushButton::clicked, this, [this, day]() {
            drawer()->closePanel(); // the agenda is behind the drawer
            emit showDayRequested(day);
        });
        v->addWidget(open);
    } else {
        v->addWidget(softLabel(tr("Nothing fits before the deadline."), row));
    }

    auto* actions = new QHBoxLayout;
    actions->setSpacing(6);
    if (placements.size() > 1) {
        auto* more = new QPushButton(
            m_expanded == id ? tr("Fewer") : tr("More…"), row);
        more->setObjectName("catchUpMore");
        more->setCursor(Qt::PointingHandCursor);
        more->setFlat(true);
        connect(more, &QPushButton::clicked, this, [this, id, now]() {
            m_expanded = (m_expanded == id) ? QString() : id;
            m_lastPrint.clear();
            refresh(now);
        });
        actions->addWidget(more);
    }

    const QString rowLabel = m_data->eventLabel(block);
    auto* done = new QPushButton(tr("Done"), row);
    done->setObjectName("catchUpDone");
    done->setToolTip(tr("It happened — I just didn't track it."));
    done->setCursor(Qt::PointingHandCursor);
    done->setFlat(true);
    connect(done, &QPushButton::clicked, this, [this, id, rowLabel, now]() {
        m_undoIds   = {id};
        m_undoLabel = tr("Marked done: %1.").arg(rowLabel);
        emit resolveRequested(id, BlockOutcome::Done);
        refresh(now);
    });
    auto* skip = new QPushButton(tr("Skip"), row);
    skip->setObjectName("catchUpSkip");
    skip->setToolTip(tr("Let this one go — deliberately."));
    skip->setCursor(Qt::PointingHandCursor);
    skip->setFlat(true);
    connect(skip, &QPushButton::clicked, this, [this, id, rowLabel, now]() {
        m_undoIds   = {id};
        m_undoLabel = tr("Skipped: %1.").arg(rowLabel);
        emit resolveRequested(id, BlockOutcome::Dropped);
        refresh(now);
    });
    actions->addWidget(done);
    actions->addWidget(skip);
    actions->addStretch(1);
    v->addLayout(actions);

    return row;
}
