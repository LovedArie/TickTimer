#include "UndoBar.h"

#include "Touch.h"   // the 48dp floor for the button on a phone
#include "Widgets.h" // isCompactScreen

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>

namespace
{
constexpr int kMargin   = 16;  // from the window's right and bottom edges
constexpr int kMaxWidth = 440; // one line of "... moved to Fri 11 Sep, 2 PM"
constexpr int kFadeMs   = 300;
} // namespace

UndoBar::UndoBar(QWidget* host) : QWidget(host)
{
    setObjectName(QStringLiteral("undoBar"));

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(16, 6, 8, 6);
    row->setSpacing(12);

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    // A stylesheet colour rather than the palette: the bar is dark in BOTH
    // themes, and theme::applyTheme repaints palettes app-wide.
    m_label->setStyleSheet(
        QStringLiteral("color:#FFFFFF; background:transparent;"));
    row->addWidget(m_label, 1);

    m_button = new QPushButton(tr("Undo"), this);
    m_button->setObjectName(QStringLiteral("undoBarButton"));
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setStyleSheet(QStringLiteral(
        "#undoBarButton { color:#8FE3CF; background:transparent; border:none;"
        " font-weight:700; padding:4px 10px; }"
        "#undoBarButton:hover { color:#FFFFFF; }"));
    m_button->setMinimumHeight(touch::sizeFor(32, isCompactScreen()));
    row->addWidget(m_button);
    connect(m_button, &QPushButton::clicked, this, [this]() {
        // Take the action out BEFORE closing: the undo may itself ask this
        // bar to say something ("Could not undo ..."), and that new offer
        // must not be wiped out by the closing of the old one.
        UndoAction undo = std::move(m_onUndo);
        m_onUndo = nullptr;
        dismiss();
        if (undo)
            undo();
    });

    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(1.0);
    setGraphicsEffect(m_opacity);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &UndoBar::fadeOut);

    host->installEventFilter(this); // the corner moves when the window does
    hide();
}

void UndoBar::offer(const QString& text, UndoAction onUndo, int msecs)
{
    if (m_fade)
        m_fade->stop(); // a new offer interrupts a fade already under way
    m_opacity->setOpacity(1.0);

    m_onUndo = std::move(onUndo);
    m_label->setText(text);
    m_button->setVisible(bool(m_onUndo));

    // As wide as one line of text needs, never wider than the window leaves
    // room for; the height then follows from that width.
    const QWidget* host = parentWidget();
    const int width =
        host ? qBound(160, host->width() - 2 * kMargin, kMaxWidth) : kMaxWidth;
    setFixedWidth(width);
    const int height = layout()->hasHeightForWidth()
                           ? layout()->totalHeightForWidth(width)
                           : layout()->totalSizeHint().height();
    setFixedHeight(qMax(44, height));

    place();
    show();
    raise(); // above the page, and above anything added to the host since
    m_timer->start(msecs);
}

void UndoBar::setBottomClearance(int px)
{
    m_bottomClearance = qMax(0, px);
    if (isVisible())
        place();
}

QString UndoBar::text() const
{
    return m_label->text();
}

bool UndoBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize
        && isVisible())
        place();
    return QWidget::eventFilter(watched, event);
}

void UndoBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    // Dark ink, not the white card reminders use: nobody should have to read
    // the words to know this is not a reminder.
    p.setBrush(QColor(0x27, 0x2C, 0x33));
    p.drawRoundedRect(rect(), 10, 10);
}

void UndoBar::place()
{
    const QWidget* host = parentWidget();
    if (!host)
        return;
    move(host->width() - width() - kMargin,
         host->height() - height() - kMargin - m_bottomClearance);
}

void UndoBar::fadeOut()
{
    if (!m_fade) {
        m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
        m_fade->setDuration(kFadeMs);
        connect(m_fade, &QPropertyAnimation::finished, this,
                &UndoBar::dismiss);
    }
    m_fade->stop();
    m_fade->setStartValue(m_opacity->opacity());
    m_fade->setEndValue(0.0);
    m_fade->start();
}

void UndoBar::dismiss()
{
    m_timer->stop();
    if (m_fade)
        m_fade->stop();
    hide();
    m_opacity->setOpacity(1.0);
    m_onUndo = nullptr; // an offer that has gone can no longer be taken up
}
