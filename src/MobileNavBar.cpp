#include "MobileNavBar.h"

#include "Theme.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>

// One tab: an icon, a checked state, and a generous hit area. QAbstractButton
// rather than QToolButton because everything QToolButton would give us here is
// something we are overriding anyway (text layout, icon scaling, the styled
// frame) — and painting it ourselves is how the icon avoids being a font bet.
class NavTabButton : public QAbstractButton
{
public:
    NavTabButton(NavIcon icon, QWidget* parent)
        : QAbstractButton(parent), m_icon(icon)
    {
        setCheckable(true);
        // NOT autoExclusive — see the header. The bar clears and sets every
        // tab by hand so that "no tab owns this page" is expressible.
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::NoFocus); // a bar tab is not a keyboard stop
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const bool on = isChecked();
        const QColor tint = on ? theme::focus() : theme::inkSoft();

        // The selected pill. Drawn behind the glyph and sized to the icon, not
        // to the tab, so a wide tab does not get a wide lozenge.
        if (on) {
            const QRectF pill(width() / 2.0 - 22, height() / 2.0 - 16, 44, 32);
            QColor wash = theme::focus();
            wash.setAlpha(28);
            p.setPen(Qt::NoPen);
            p.setBrush(wash);
            // Radius is exactly half the height: QSS drops a larger one
            // silently, and while this is drawn by hand the same restraint
            // keeps the shape honest if it ever moves into a stylesheet.
            p.drawRoundedRect(pill, 16, 16);
        }

        const QRectF box(width() / 2.0 - 11, height() / 2.0 - 11, 22, 22);
        paintNavIcon(p, box, m_icon, tint);
    }

    QSize sizeHint() const override { return {56, 56}; }

private:
    NavIcon m_icon;
};

// ---- the glyphs -------------------------------------------------------------
//
// All five are stroked line art on a square box, at a stroke width that stays
// legible at 22 logical px (66 physical at dpr 3). Deliberately plain: an icon
// nobody can name is worse than a word, and these have to read at a glance
// with no label under them.

void paintNavIcon(QPainter& p, const QRectF& box, NavIcon icon,
                  const QColor& colour)
{
    p.save();
    QPen pen(colour, 1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal x = box.x();
    const qreal y = box.y();
    const qreal w = box.width();
    const qreal h = box.height();

    switch (icon) {
    case NavIcon::Calendar: {
        // A page with a bound top edge and two hanging rings.
        const QRectF body(x, y + h * 0.15, w, h * 0.85);
        p.drawRoundedRect(body, 3, 3);
        p.drawLine(QPointF(x, y + h * 0.42), QPointF(x + w, y + h * 0.42));
        p.drawLine(QPointF(x + w * 0.28, y), QPointF(x + w * 0.28, y + h * 0.28));
        p.drawLine(QPointF(x + w * 0.72, y), QPointF(x + w * 0.72, y + h * 0.28));
        break;
    }
    case NavIcon::Upcoming: {
        // A list: three rules, each with its own bullet.
        for (int i = 0; i < 3; ++i) {
            const qreal ly = y + h * (0.2 + i * 0.3);
            p.setBrush(colour);
            p.drawEllipse(QPointF(x + w * 0.09, ly), 1.6, 1.6);
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(x + w * 0.32, ly), QPointF(x + w, ly));
        }
        break;
    }
    case NavIcon::Assistant: {
        // A four-point spark — the replacement for the ✦ that renders as an
        // empty box on Android, which is the whole reason these are painted.
        QPainterPath star;
        const QPointF c(x + w * 0.5, y + h * 0.5);
        const qreal r = w * 0.5;
        const qreal waist = r * 0.30;
        star.moveTo(c.x(), c.y() - r);
        star.cubicTo(c.x() + waist, c.y() - waist, c.x() + waist, c.y() - waist,
                     c.x() + r, c.y());
        star.cubicTo(c.x() + waist, c.y() + waist, c.x() + waist, c.y() + waist,
                     c.x(), c.y() + r);
        star.cubicTo(c.x() - waist, c.y() + waist, c.x() - waist, c.y() + waist,
                     c.x() - r, c.y());
        star.cubicTo(c.x() - waist, c.y() - waist, c.x() - waist, c.y() - waist,
                     c.x(), c.y() - r);
        p.drawPath(star);
        break;
    }
    case NavIcon::Activities: {
        // Stacked layers — life areas, one behind another.
        QPainterPath top;
        top.moveTo(x + w * 0.5, y + h * 0.08);
        top.lineTo(x + w, y + h * 0.34);
        top.lineTo(x + w * 0.5, y + h * 0.60);
        top.lineTo(x, y + h * 0.34);
        top.closeSubpath();
        p.drawPath(top);
        p.drawPolyline(QPolygonF()
                       << QPointF(x, y + h * 0.64)
                       << QPointF(x + w * 0.5, y + h * 0.90)
                       << QPointF(x + w, y + h * 0.64));
        break;
    }
    case NavIcon::Pomodoro: {
        // A timer: a dial, a stem, and one hand.
        const QRectF dial(x + w * 0.08, y + h * 0.20, w * 0.84, h * 0.76);
        p.drawEllipse(dial);
        p.drawLine(QPointF(x + w * 0.34, y), QPointF(x + w * 0.66, y));
        p.drawLine(QPointF(x + w * 0.5, y), QPointF(x + w * 0.5, y + h * 0.20));
        p.drawLine(QPointF(x + w * 0.5, y + h * 0.58),
                   QPointF(x + w * 0.5, y + h * 0.34));
        break;
    }
    }
    p.restore();
}

// ---- the bar ----------------------------------------------------------------

MobileNavBar::MobileNavBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("mobileNav"));
    // WA_StyledBackground so the objectName rule in the theme actually paints;
    // a plain QWidget ignores background from a stylesheet otherwise. Same
    // attribute TaskDetailPanel needs for the same reason.
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(58);

    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(0);
}

void MobileNavBar::addTab(int pageIndex, NavIcon icon, const QString& tooltip)
{
    auto* b = new NavTabButton(icon, this);
    b->setToolTip(tooltip);
    // The tab reports which PAGE it wants; it never touches the stack. Same
    // "widget reports, owner decides" contract the pages already follow.
    connect(b, &QAbstractButton::clicked, this,
            [this, pageIndex]() { emit pageRequested(pageIndex); });
    m_row->addWidget(b, 1);
    m_tabs.append({pageIndex, b});
}

void MobileNavBar::setCurrentPage(int pageIndex)
{
    // Every tab set explicitly, including to false. See the header: an
    // autoExclusive group cannot express "none of these", and pages without a
    // tab (Special days, Archive) need exactly that.
    for (const Tab& t : m_tabs)
        t.button->setChecked(t.pageIndex == pageIndex);
}
