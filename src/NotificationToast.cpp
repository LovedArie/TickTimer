#include "NotificationToast.h"

#include "Theme.h"

#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int kWidth  = 340;
constexpr int kMargin = 16; // from the screen edge, and between toasts
} // namespace

QList<QPointer<NotificationToast>> NotificationToast::s_live;

NotificationToast* NotificationToast::show(const ToastSpec& spec)
{
    auto* toast = new NotificationToast(spec);
    s_live.append(QPointer<NotificationToast>(toast));
    restack();
    toast->QWidget::show();
    return toast;
}

NotificationToast* NotificationToast::show(const QString& title,
                                           const QString& body, int msecs)
{
    ToastSpec spec;
    spec.title = title;
    spec.body  = body;
    spec.msecs = msecs;
    return show(spec);
}

NotificationToast::NotificationToast(const ToastSpec& spec)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint
                           | Qt::WindowStaysOnTopHint)
    , m_spec(spec)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating); // never steal the keyboard
    setAttribute(Qt::WA_QuitOnClose, false);    // a toast can't hold the app
    setAttribute(Qt::WA_DeleteOnClose);         // fire-and-forget lifetime
    setFixedWidth(kWidth);
    setCursor(Qt::PointingHandCursor);

    auto* titleLabel = new QLabel(m_spec.title, this);
    titleLabel->setStyleSheet(
        "font-size:13px; font-weight:800; color:#2A2F36;");
    titleLabel->setWordWrap(true);
    auto* bodyLabel = new QLabel(m_spec.body, this);
    bodyLabel->setStyleSheet("font-size:12px; color:#616974;");
    bodyLabel->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 12, 14, 12); // extra left: the accent bar
    layout->setSpacing(3);
    layout->addWidget(titleLabel);
    layout->addWidget(bodyLabel);

    // The optional action (v28.0 seam, first consumer arrives with §G.1's
    // check-in). A copy of the callback, run BEFORE close: close deletes
    // this toast (WA_DeleteOnClose), and a lambda reading members of a
    // deleted widget is the bug the copy exists to prevent.
    if (!m_spec.actionText.isEmpty() && m_spec.onAction) {
        auto* action = new QPushButton(m_spec.actionText, this);
        action->setCursor(Qt::PointingHandCursor);
        action->setStyleSheet(
            "QPushButton { border:none; background:transparent; padding:0; "
            "text-align:left; font-size:12px; font-weight:700; color:#2F7E6E; }"
            "QPushButton:hover { color:#245F53; }");
        const std::function<void()> callback = m_spec.onAction;
        connect(action, &QPushButton::clicked, this,
                [this, callback]() {
                    callback();
                    close();
                });
        layout->addWidget(action, 0, Qt::AlignLeft);
    }
    adjustSize();

    // Departure is scheduled at birth: fade for the last 300 ms of life,
    // then close (which deletes, which restacks the survivors).
    QTimer::singleShot(qMax(0, m_spec.msecs - 300), this,
                       &NotificationToast::fadeOutAndClose);
    // Timing trap (caught by the stacking test): destroyed() is emitted
    // WHILE ~QObject runs, and QPointers to the dying object are not
    // guaranteed null yet at that instant — so a restack that trusts the
    // null-out still counts the departed and parks the survivors one slot
    // low. Remove the sender explicitly; trust nothing mid-destruction.
    connect(this, &QObject::destroyed, [](QObject* gone) {
        for (int i = s_live.size(); i-- > 0;)
            if (!s_live[i]
                || static_cast<QObject*>(s_live[i].data()) == gone)
                s_live.removeAt(i);
        restack();
    });
}

void NotificationToast::fadeOutAndClose()
{
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(300);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    connect(fade, &QPropertyAnimation::finished, this, &QWidget::close);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void NotificationToast::moveTo(const QPoint& target)
{
    // One line today. The point is that it is the ONLY line: restack()
    // decides where, this decides how, and "how" can become an animation
    // without a second call site learning about it.
    move(target);
}

void NotificationToast::restack()
{
    s_live.removeAll(nullptr); // QPointer nulls out the departed
    const QRect screen =
        QGuiApplication::primaryScreen()->availableGeometry();
    int y = screen.top() + kMargin;
    for (const auto& toast : s_live) {
        if (!toast)
            continue;
        toast->moveTo(QPoint(screen.right() - kWidth - kMargin, y));
        y += toast->height() + kMargin / 2;
    }
}

void NotificationToast::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 32), 1));
    p.setBrush(theme::surface());
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);
    // The accent bar — the app's voice-print, so a glance says "TickTimer"
    // before a word is read. Kind picks the ink (v28.0): Info keeps the
    // focus green; Alert borrows the danger red the app already uses for
    // overdue. Same bar, different voice — recognisable AND legible.
    p.setPen(Qt::NoPen);
    p.setBrush(m_spec.kind == ToastSpec::Kind::Alert ? theme::danger()
                                                     : theme::focus());
    p.drawRoundedRect(QRect(8, 10, 4, height() - 20), 2, 2);
}

void NotificationToast::mousePressEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    close(); // read it, tap it away — no OS notification-center archaeology
}
