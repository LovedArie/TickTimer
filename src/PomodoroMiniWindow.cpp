#include "PomodoroMiniWindow.h"

#include "PomodoroEngine.h"
#include "Prefs.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

PomodoroMiniWindow::PomodoroMiniWindow(PomodoroEngine* engine)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint
                           | Qt::WindowStaysOnTopHint)
    , m_engine(engine)
{
    // No parent — see the header's v19.5.1 note: on Windows a parent makes
    // this an OWNED window, and owned windows hide when their owner
    // minimizes. Parentless costs us Qt's automatic cleanup and its
    // last-window-closed accounting; both are paid back explicitly —
    // deletion by PomodoroPage's destructor, and this attribute so an open
    // card never keeps the app alive after the main window closes:
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(210, 74);

    // ---- left: the circular play/pause ------------------------------------
    m_playBtn = new QPushButton(this);
    m_playBtn->setFixedSize(38, 38);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    connect(m_playBtn, &QPushButton::clicked,
            m_engine, &PomodoroEngine::toggle);

    // ---- middle: phase (a quiet skip button) over the countdown -----------
    m_phaseBtn = new QToolButton(this);
    m_phaseBtn->setCursor(Qt::PointingHandCursor);
    m_phaseBtn->setToolTip(tr("Skip to the next phase"));
    m_phaseBtn->setStyleSheet(
        "QToolButton { border:none; background:transparent; color:#8A9098;"
        " font-size:11px; font-weight:700; padding:0; }"
        "QToolButton:hover { color:#4A505A; }");
    connect(m_phaseBtn, &QToolButton::clicked,
            m_engine, &PomodoroEngine::skip);

    m_timeLabel = new QLabel(this);
    m_timeLabel->setStyleSheet(
        "font-size:21px; font-weight:800; color:#2A2F36;"
        "font-variant-numeric: tabular-nums;");

    auto* middle = new QVBoxLayout;
    middle->setContentsMargins(0, 0, 0, 0);
    middle->setSpacing(0);
    middle->addWidget(m_phaseBtn, 0, Qt::AlignLeft);
    middle->addWidget(m_timeLabel, 0, Qt::AlignLeft);

    // ---- top right: expand / close, styled like the screenshot's ghosts ---
    const auto ghost = [this](const QString& glyph, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(glyph);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(18, 18);
        b->setStyleSheet(
            "QToolButton { border:none; background:transparent;"
            " color:#B7BCC3; font-size:11px; padding:0; }"
            "QToolButton:hover { color:#4A505A; }");
        return b;
    };
    auto* expandBtn = ghost(QStringLiteral("⤢"), tr("Back to the app"));
    auto* closeBtn  = ghost(QStringLiteral("✕"), tr("Close mini timer"));
    connect(expandBtn, &QToolButton::clicked, this, [this]() {
        emit expandRequested();
        hide();
    });
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::hide);

    auto* corner = new QVBoxLayout;
    corner->setContentsMargins(0, 2, 2, 0);
    corner->setSpacing(0);
    auto* cornerRow = new QHBoxLayout;
    cornerRow->setSpacing(0);
    cornerRow->addWidget(expandBtn);
    cornerRow->addWidget(closeBtn);
    corner->addLayout(cornerRow);
    corner->addStretch(1);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 6, 8);
    layout->setSpacing(10);
    layout->addWidget(m_playBtn);
    layout->addLayout(middle, 1);
    layout->addLayout(corner);

    // One clock, two faces: every repaint cue comes from the shared engine.
    connect(m_engine, &PomodoroEngine::changed,
            this, &PomodoroMiniWindow::refresh);

    // Reopen where it was last left (invalid point on first ever run —
    // then Qt's default placement is fine).
    const QPoint saved = prefs::pomodoroMiniPos();
    if (!saved.isNull())
        move(saved);

    refresh();
}

void PomodoroMiniWindow::refresh()
{
    const QColor accent = (m_engine->phase() == PomodoroEngine::Phase::Focus)
                              ? theme::focus()
                              : theme::brk();

    // ▶ / ❚❚ — text glyphs, not icons: zero resources, themed by stylesheet.
    m_playBtn->setText(m_engine->running() ? QStringLiteral("❚❚")
                                           : QStringLiteral("▶"));
    m_playBtn->setStyleSheet(
        QStringLiteral(
            "QPushButton { border-radius:19px; border:none; color:white;"
            " background:%1; font-size:13px; font-weight:900; }"
            "QPushButton:hover { background:%2; }")
            .arg(accent.name(), accent.darker(112).name()));

    m_phaseBtn->setText(m_engine->phaseName() + QStringLiteral("  ›"));
    m_timeLabel->setText(m_engine->timeText());
}

void PomodoroMiniWindow::paintEvent(QPaintEvent*)
{
    // The card: rounded surface + hairline border, painted because a
    // translucent frameless window starts as NOTHING — every visible pixel
    // is ours to supply (same own-every-pixel rule as the custom widgets).
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 28), 1));
    p.setBrush(theme::surface());
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 14, 14);
}

void PomodoroMiniWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging   = true;
        m_dragAnchor = event->position().toPoint(); // where IN the card
    }
    QWidget::mousePressEvent(event);
}

void PomodoroMiniWindow::mouseMoveEvent(QMouseEvent* event)
{
    // Frameless = no OS title bar to grab, so the whole card is the handle:
    // keep the point you grabbed under the cursor as it moves.
    if (m_dragging)
        move(event->globalPosition().toPoint() - m_dragAnchor);
    QWidget::mouseMoveEvent(event);
}

void PomodoroMiniWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging) {
        m_dragging = false;
        prefs::setPomodoroMiniPos(pos()); // remember across sessions
    }
    QWidget::mouseReleaseEvent(event);
}
