#include "SettingsDialog.h"

#include "SettingsPages.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setModal(true);

    m_nav = new QListWidget(this);
    m_nav->setObjectName("settingsNav");
    // A fixed, narrow column. Not a splitter: the nav has one job and no
    // reason to be resizable, and every degree of freedom in a layout is a
    // state that has to look right.
    m_nav->setFixedWidth(168);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_stack = new QStackedWidget(this);

    // The tallest page decides the dialog's height (QStackedWidget's size
    // hint is the max of its children), so switching pages never resizes the
    // window — a dialog that jumps when you click a nav row feels broken.
    // The scroll area is the escape valve for a small screen.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_stack);

    // Pages are constructed EAGERLY, all of them, right now.
    //
    // Lazy construction (build a page the first time it's shown) is the
    // obvious optimisation and it is the wrong call here for two reasons.
    // First, test_ui.cpp reaches in with findChild<QComboBox*>("aiProviderCombo")
    // and friends — a page that doesn't exist yet has no children to find,
    // and eight tests would start failing for reasons that have nothing to
    // do with settings. Second, the saving is imaginary: these are a few
    // dozen widgets, built once, on a click the user made deliberately.
    // Optimising a cost nobody can perceive by breaking the test suite is a
    // bad trade twice over.
    addPage(new AgendaSettingsPage(this));
    addPage(new NeedsBlockSettingsPage(this));
    addPage(new CatchUpSettingsPage(this)); // v26.2 — the refactor's payoff:
                                            // this line is the WHOLE cost of
                                            // a new settings section
    addPage(new AssistantSettingsPage(this));

    // Nav drives stack. No lambda, no glue: currentRowChanged carries an int
    // and setCurrentIndex takes an int, and addPage() guarantees the two
    // index spaces agree. When two Qt classes already speak the same
    // language, connecting them directly is better than translating between
    // them — fewer moving parts, and the compiler checks the signature.
    connect(m_nav, &QListWidget::currentRowChanged,
            m_stack, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(20);
    body->addWidget(m_nav);
    body->addWidget(scroll, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);

    // Wide enough for the Assistant page's key field to be readable without
    // horizontal scrolling; tall enough that the short pages don't look
    // stranded in whitespace.
    resize(720, 540);
}

void SettingsDialog::addPage(SettingsPage* page)
{
    m_nav->addItem(page->title());
    m_stack->addWidget(page);
    m_pages.append(page);
}

void SettingsDialog::save()
{
    // One pass, in nav order. The dialog does not know what any page stores
    // and never has to — adding a page adds zero lines here, which is the
    // entire point of the refactor.
    for (SettingsPage* page : m_pages)
        page->save();
}
