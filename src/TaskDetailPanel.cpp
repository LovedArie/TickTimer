#include "TaskDetailPanel.h"

#include "AppData.h"
#include "TaskDetailDialog.h" // the shared free helpers (seed / apply)
#include "TaskDetailForm.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

TaskDetailPanel::TaskDetailPanel(AppData* data, QWidget* host)
    : QWidget(host), m_data(data), m_host(host)
{
    setObjectName(QStringLiteral("taskDetailPanel"));

    // A plain QWidget ignores stylesheet backgrounds unless told to paint
    // them — invisible while the panel sat IN the layout over a white
    // page, fatal as an overlay: without this line the drawer is a ghost
    // and the page shows straight through it.
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "#taskDetailPanel { background: #FFFFFF; "
        "border-left: 1px solid #E2E6E0; }"));

    if (m_host) {
        // The scrim: the "lower contrast" the overlay owes the screen
        // behind it. Host-sized, translucent ink, and CLICKABLE — a click
        // on it is a click "anywhere else", routed through the same
        // guarded close as ✕ and Esc. Created before the drawer so the
        // natural z-order is scrim below, drawer above.
        m_scrim = new QWidget(m_host);
        m_scrim->setObjectName(QStringLiteral("panelScrim"));
        m_scrim->setAttribute(Qt::WA_StyledBackground, true);
        m_scrim->setStyleSheet(QStringLiteral(
            "#panelScrim { background: rgba(43, 47, 54, 0.28); }"));
        m_scrim->hide();
        m_scrim->installEventFilter(this);

        m_host->installEventFilter(this); // follow the window's resizes
        hide();                           // closed until openTask
    } else {
        // Hostless (tests): no overlay machinery; width still gates
        // visibility so isOpen has a physical meaning.
        setMinimumWidth(0);
        setMaximumWidth(0);
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(8);

    // ---- header: what you're looking at, and the way out -------------------
    auto* headerRow = new QHBoxLayout;
    m_header = new QLabel(tr("Task details"), this);
    m_header->setStyleSheet(
        QStringLiteral("font-weight:600; color:#2B2F36;"));
    m_close = new QToolButton(this);
    m_close->setObjectName(QStringLiteral("panelCloseButton"));
    m_close->setText(QStringLiteral("\u2715"));
    m_close->setAutoRaise(true);
    m_close->setToolTip(tr("Close (Esc, or click outside) — asks first if "
                           "you have unsaved changes"));
    connect(m_close, &QToolButton::clicked,
            this, &TaskDetailPanel::closePanel);
    headerRow->addWidget(m_header, 1);
    headerRow->addWidget(m_close);
    layout->addLayout(headerRow);

    // ---- the form, scrollable ----------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_scroll, 1);

    // ---- footer: the explicit save, with feedback both ways ----------------
    auto* footer = new QHBoxLayout;
    m_savedFlash = new QLabel(tr("Saved \u2713"), this);
    m_savedFlash->setObjectName(QStringLiteral("panelSavedFlash"));
    m_savedFlash->setStyleSheet(QStringLiteral("color:#2F7E6E;"));
    m_savedFlash->hide();
    m_save = new QPushButton(tr("Save"), this);
    m_save->setObjectName(QStringLiteral("panelSaveButton"));
    m_save->setDefault(false);
    footer->addWidget(m_savedFlash);
    footer->addStretch(1);
    footer->addWidget(m_save);
    layout->addLayout(footer);
    connect(m_save, &QPushButton::clicked, this, [this]() { saveNow(); });

    // The slide is a POSITION animation now, not a width one: the drawer
    // keeps its full width and moves from just past the host's right edge
    // to flush against it — content never reflows, which is the entire
    // point of overlay over docked.
    m_anim = new QPropertyAnimation(this, "pos", this);
    m_anim->setDuration(180);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_open && m_host) {
            hide(); // fully off-stage only after the slide-out lands
            m_scrim->hide();
        }
    });

    if (m_data)
        connect(m_data, &AppData::changed, this, [this]() {
            if (m_applying || m_taskId.isEmpty())
                return;
            if (!m_data->taskById(m_taskId)) {
                m_taskId.clear(); // the task is GONE — nothing to guard
                animateTo(false);
                return;
            }
            if (m_form && !m_form->isDirty())
                buildFor(m_taskId); // clean form: show the new truth
            // dirty form: the user's edits win the race; Save writes last
        });
}

int TaskDetailPanel::panelWidth() const
{
    // 440, unless the window is narrow: an overlay that covers the whole
    // screen is a modal with extra steps, so at least kKeepClear px of
    // dimmed-but-visible content always survives on the left.
    if (!m_host)
        return kWidth;
    return qMin(kWidth, qMax(200, m_host->width() - kKeepClear));
}

void TaskDetailPanel::openTask(const QString& taskId)
{
    if (taskId.isEmpty())
        return;

    if (m_open && taskId == m_taskId) {
        animateTo(true); // already showing it — just make sure it's open
        return;
    }

    if (m_form && m_form->isDirty() && !resolveUnsavedEdits())
        return; // Stay means the click did nothing at all

    buildFor(taskId);
    animateTo(true);
}

void TaskDetailPanel::focusTitleForNaming()
{
    if (m_form)
        m_form->selectTitleForNaming();
}

void TaskDetailPanel::closePanel()
{
    if (!m_open)
        return;
    if (m_form && m_form->isDirty() && !resolveUnsavedEdits())
        return; // Stay: the panel remains, edits intact

    m_taskId.clear();
    animateTo(false);
}

void TaskDetailPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        closePanel();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool TaskDetailPanel::eventFilter(QObject* watched, QEvent* event)
{
    // The scrim IS the "anywhere else": any press on it routes through
    // the guarded close. Swallowed either way — a click that closed (or
    // was told to Stay) should not ALSO land on whatever sat underneath.
    if (m_scrim && watched == m_scrim
        && event->type() == QEvent::MouseButtonPress) {
        closePanel();
        return true;
    }
    // The drawer hugs the host's right edge through every resize; done
    // instantly (no animation) — a resize is not an entrance.
    if (m_host && watched == m_host && event->type() == QEvent::Resize
        && m_open) {
        repositionOverlay();
    }
    return QWidget::eventFilter(watched, event);
}

void TaskDetailPanel::repositionOverlay()
{
    if (!m_host)
        return;
    const QRect r = m_host->rect();
    m_scrim->setGeometry(r);
    resize(panelWidth(), r.height());
    move(r.width() - panelWidth(), 0);
}

void TaskDetailPanel::buildFor(const QString& taskId)
{
    const Task* task = m_data ? m_data->taskById(taskId) : nullptr;
    if (!task) {
        m_taskId.clear();
        animateTo(false);
        return;
    }
    const Task snapshot = *task; // the vector may move under the seeds

    m_taskId = taskId;
    m_header->setText(snapshot.isPiece() ? tr("Piece details")
                                         : tr("Task details"));

    // Rebuild, don't reset — and deleteLater, not delete: we may be
    // inside the OLD form's navigateRequested emission right now (the
    // queued connection below is the other half of this safety).
    if (m_form)
        m_form->deleteLater();
    m_form = new TaskDetailForm(snapshot.title, snapshot.description,
                                snapshot.dueDate, snapshot.dueTime,
                                snapshot.repeat, snapshot.priority,
                                snapshot.estimateMinutes, snapshot.chunkable);

    if (snapshot.isPiece())
        if (const Task* parent = m_data->taskById(snapshot.parentId))
            m_form->setBreadcrumb(parent->id, parent->title);

    seedTaskDetailPieces(*m_form, *m_data, taskId); // no-op for a piece

    connect(m_form, &TaskDetailForm::navigateRequested, this,
            &TaskDetailPanel::openTask, Qt::QueuedConnection);
    connect(m_form, &TaskDetailForm::edited,
            this, &TaskDetailPanel::updateSaveUi);

    m_scroll->setWidget(m_form);
    // The Theme.h v3 trap, re-armed by this very call and disarmed on the
    // line after: setWidget() turns ON the child's autoFillBackground, so
    // the form — which never painted a background in its life — starts
    // filling itself with the palette's Window grey while the panel
    // around it paints white. The v28.6.1 patchwork the owner saw was
    // exactly this seam. OFF again: the panel's white shows through,
    // uniform. (Second documented landmine this project has re-hit; the
    // tripwire rule applies — see the session note.)
    m_form->setAutoFillBackground(false);
    updateSaveUi();
}

void TaskDetailPanel::saveNow()
{
    if (m_taskId.isEmpty() || !m_form)
        return;

    m_applying = true;
    applyTaskDetailAnswers(*m_data, m_taskId, *m_form);
    m_applying = false;

    buildFor(m_taskId); // newborn pieces gain ids — and doors — now

    m_savedFlash->show();
    QTimer::singleShot(1500, m_savedFlash, &QLabel::hide);
}

bool TaskDetailPanel::resolveUnsavedEdits()
{
    UnsavedChoice choice;
    if (m_promptOverride) {
        choice = m_promptOverride();
    } else {
        const auto pressed = QMessageBox::question(
            this, tr("Unsaved changes"),
            tr("Save your changes to \"%1\"?")
                .arg(m_form->chosenTitle()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save); // Save is the default: Enter never discards
        choice = pressed == QMessageBox::Save      ? UnsavedChoice::Save
                 : pressed == QMessageBox::Discard ? UnsavedChoice::Discard
                                                   : UnsavedChoice::Stay;
    }

    switch (choice) {
    case UnsavedChoice::Save:
        m_applying = true;
        applyTaskDetailAnswers(*m_data, m_taskId, *m_form);
        m_applying = false;
        return true;
    case UnsavedChoice::Discard:
        return true;
    case UnsavedChoice::Stay:
    default:
        return false;
    }
}

void TaskDetailPanel::updateSaveUi()
{
    m_save->setEnabled(m_form && m_form->isDirty());
}

void TaskDetailPanel::animateTo(bool open)
{
    m_open = open;

    if (!m_host) {
        // Hostless: no stage to slide across — width gates visibility.
        setMaximumWidth(open ? kWidth : 0);
        if (open)
            setFocus();
        return;
    }

    const QRect r = m_host->rect();
    const int   w = panelWidth();
    resize(w, r.height());
    const QPoint shown(r.width() - w, 0);
    const QPoint hidden(r.width(), 0); // just past the edge

    if (open) {
        m_scrim->setGeometry(r);
        m_scrim->show();
        m_scrim->raise();
        if (!isVisible())
            move(hidden); // first entrance starts off-stage
        show();
        raise(); // drawer above scrim, scrim above content
        setFocus();
    }

    m_anim->stop();
    m_anim->setStartValue(pos());
    m_anim->setEndValue(open ? shown : hidden);
    m_anim->start(); // slide-out's finished() hides drawer + scrim
}
