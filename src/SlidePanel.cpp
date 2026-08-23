#include "SlidePanel.h"

#include "Theme.h"
#include "Widgets.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace
{
constexpr int kSheetMaxW = 340; // a reading column, not a takeover
constexpr int kSlideMs   = 180; // fast enough to feel like a gesture,
                                // slow enough to SHOW where the sheet
                                // came from — motion is the affordance
                                // that teaches "swipe/press to send it
                                // back"
} // namespace

SlidePanel::SlidePanel(QWidget* host)
    : QWidget(host)
    , m_host(host)
{
    // The panel is a CHILD covering the host, not a window. Children
    // clip, move and stack with their parent for free — the entire
    // "drawer follows the panel around" problem dissolves into Qt's
    // ordinary parent/child rules.
    setAttribute(Qt::WA_StyledBackground, false);
    hide();

    m_sheet = new QFrame(this);
    m_sheet->setObjectName("slideSheet");
    m_sheet->setStyleSheet(QStringLiteral(
        "#slideSheet { background:%1; border:1px solid %2; "
        "border-top-left-radius:12px; border-bottom-left-radius:12px; }")
                               .arg(theme::surface().name(),
                                    theme::line().name()));

    auto* v = new QVBoxLayout(m_sheet);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(8);

    auto* head = new QHBoxLayout;
    m_title = new QLabel(m_sheet);
    m_title->setObjectName("h2");
    // U+00D7, not U+2715. The prettier multiplication X renders as an empty
    // box on Android — the same font lottery that ate the rail's ✦ and the
    // reason the bottom bar's icons are painted. This one is Latin-1.
    auto* close = new QPushButton(QString(QChar(0x00D7)), m_sheet);
    close->setObjectName("slideClose");
    close->setFlat(true);
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedSize(28, 28);
    m_headerBtn = close;
    connect(close, &QPushButton::clicked, this, [this]() {
        if (m_headerBtnCloses)
            closePanel();
        else
            emit headerButtonClicked();
    });
    head->addWidget(m_title, 1);
    head->addWidget(close);
    v->addLayout(head);

    // The rows live in a scroll area for the same reason the card's do:
    // the sheet's height is the host's height, a fact the CONTENT must
    // never be allowed to negotiate with (v22's frozen-window lesson,
    // applied at birth this time instead of retrofitted).
    auto* scroll = new QScrollArea(m_sheet);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    m_content = new QVBoxLayout(body);
    m_content->setContentsMargins(0, 0, 0, 0);
    m_content->setSpacing(6);
    m_content->addStretch(1); // rows insert ABOVE this; slack pools below
    scroll->setWidget(body);
    v->addWidget(scroll, 1);

    // Sliding animates the sheet's POSITION, not its size: resize would
    // relayout the rows every frame (expensive and jittery); a moving
    // rect of finished pixels is what compositors are built for.
    m_anim = new QPropertyAnimation(m_sheet, "pos", this);
    m_anim->setDuration(kSlideMs);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);

    m_host->installEventFilter(this); // follow the host through resizes
}

void SlidePanel::setTitle(const QString& title)
{
    m_title->setText(title);
}

void SlidePanel::clearContent()
{
    // Same deleteLater discipline as the card's rebuild: never delete a
    // widget that might be in the middle of delivering the click that
    // caused this clear.
    while (m_content->count() > 1) { // keep the trailing stretch
        QLayoutItem* item = m_content->takeAt(0);
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
}

void SlidePanel::placeSheet(bool shown)
{
    const int w = qMin(kSheetMaxW, m_host->width() - 24);
    m_sheet->resize(w, m_host->height());
    m_sheet->move(shown ? m_host->width() - w : m_host->width(), 0);
}

void SlidePanel::open()
{
    if (m_open)
        return;
    m_open = true;
    emit openStateChanged(true);
    setGeometry(m_host->rect());
    placeSheet(false);          // start just off the right edge…
    show();
    raise();
    setFocus(Qt::PopupFocusReason); // so Esc lands here
    const int w = qMin(kSheetMaxW, m_host->width() - 24);
    m_anim->stop();
    m_anim->setStartValue(m_sheet->pos());
    m_anim->setEndValue(QPoint(m_host->width() - w, 0)); // …slide to rest
    m_anim->start();
}

void SlidePanel::closePanel()
{
    if (!m_open)
        return;
    m_open = false;
    m_anim->stop();
    m_anim->setStartValue(m_sheet->pos());
    m_anim->setEndValue(QPoint(m_host->width(), 0));
    // hide() when the exit finishes — disconnect first so an open()
    // arriving mid-close can't inherit this hide and blank a panel the
    // user just asked for.
    disconnect(m_anim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        disconnect(m_anim, &QPropertyAnimation::finished, this, nullptr);
        if (!m_open)
            hide();
    });
    m_anim->start();
    emit closed();
    emit openStateChanged(false);
}

bool SlidePanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_host && event->type() == QEvent::Resize && m_open) {
        // Jump, don't animate: a resize is the WINDOW moving, and the
        // drawer keeping formation is the non-event; animating it would
        // draw attention to exactly nothing.
        setGeometry(m_host->rect());
        placeSheet(true);
    }
    return QWidget::eventFilter(watched, event);
}

void SlidePanel::keyPressEvent(QKeyEvent* event)
{
    // Escape on a desktop, Back on Android — the same intent, two keys.
    //
    // Key_Back matters more than it looks: this is a plain child widget, not a
    // QDialog, so the app-wide dialog fitter's Back mapping never reaches it.
    // Without this line Back fell through to the activity and QUIT THE APP
    // from an open drawer, which on the phone reads as a crash.
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Back) {
        closePanel();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SlidePanel::mousePressEvent(QMouseEvent* event)
{
    // A press that reached the PANEL (not the sheet — the sheet is a
    // child and takes its own events) is a scrim tap: the universal
    // "put it back" gesture.
    Q_UNUSED(event);
    closePanel();
}

void SlidePanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QColor scrim = theme::ink();
    scrim.setAlphaF(0.18f); // present enough to say "this layer is modal-
                            // ish", light enough to keep the day visible
                            // behind it — the drawer is a glance aside,
                            // not a change of subject
    p.fillRect(rect(), scrim);
}

void SlidePanel::setHeaderButton(const QString& text, const QString& tooltip)
{
    if (!m_headerBtn)
        return;
    m_headerBtnCloses = text.isEmpty();
    m_headerBtn->setText(m_headerBtnCloses ? QString(QChar(0x00D7)) : text);
    m_headerBtn->setToolTip(tooltip);
    // A word needs room; the ✕ is a square.
    m_headerBtn->setFixedSize(m_headerBtnCloses ? QSize(28, 28)
                                                : QSize(110, 30));
}
