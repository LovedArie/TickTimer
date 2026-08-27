#include "SettingsDialog.h"

#include "SettingsPages.h"

#include "Widgets.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent, QString memoryPath)
    : QDialog(parent), m_memoryPath(std::move(memoryPath))
{
    setWindowTitle(tr("Settings"));
    setModal(true);

    const bool compact = isCompactScreen();

    m_nav = new QListWidget(this);
    m_nav->setObjectName("settingsNav");
    // A fixed, narrow column. Not a splitter: the nav has one job and no
    // reason to be resizable, and every degree of freedom in a layout is a
    // state that has to look right.
    m_nav->setFixedWidth(168);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    makeTouchScrollable(m_nav);

    // ---- the phone's section switcher (v30.8) ---------------------------
    // 168px of nav plus 20 of spacing plus 40 of margin leaves 132dp of a
    // 360dp screen for the settings themselves, so every page scrolled
    // sideways. A phone gets a single-line picker ABOVE the pages instead —
    // the same trade the Activities page made when its life-area name
    // became the switcher: one labelled control that says where you are and
    // changes it, rather than a permanent column that only says.
    //
    // BOTH are built, always, and only one is ever visible. That is the
    // watcher's rule (a mode change may not create or destroy widgets) and
    // it also keeps the tests that reach in by objectName working on either
    // device.
    m_sectionPicker = new QComboBox(this);
    m_sectionPicker->setObjectName("settingsSection");
    m_nav->setVisible(!compact);
    m_sectionPicker->setVisible(compact);

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
    // v30.0 — the memory file (§L). The path arrives from the composition
    // root because only IT knows which account is logged in; an empty one
    // means the global file, the same fallback JsonStore already uses.
    addPage(new MemorySettingsPage(m_memoryPath, this));

    // Nav drives stack. No lambda, no glue: currentRowChanged carries an int
    // and setCurrentIndex takes an int, and addPage() guarantees the two
    // index spaces agree. When two Qt classes already speak the same
    // language, connecting them directly is better than translating between
    // them — fewer moving parts, and the compiler checks the signature.
    connect(m_nav, &QListWidget::currentRowChanged,
            m_stack, &QStackedWidget::setCurrentIndex);
    // The picker drives the same int, and the STACK is what keeps the two
    // in step — whichever control moved, the other follows the result. Going
    // control-to-control instead would be a feedback loop; going through the
    // thing they both change cannot be, because setCurrentIndex to the index
    // it already holds emits nothing.
    connect(m_sectionPicker, &QComboBox::currentIndexChanged,
            m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_stack, &QStackedWidget::currentChanged, this, [this](int i) {
        QSignalBlocker navQuiet(m_nav);
        QSignalBlocker pickerQuiet(m_sectionPicker);
        m_nav->setCurrentRow(i);
        m_sectionPicker->setCurrentIndex(i);
    });
    m_nav->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Beside on a desktop, above on a phone — one constructor argument, not
    // two layouts to keep in step.
    auto* body = new QBoxLayout(compact ? QBoxLayout::TopToBottom
                                        : QBoxLayout::LeftToRight);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(compact ? 8 : 20);
    body->addWidget(m_nav);
    body->addWidget(m_sectionPicker);
    body->addWidget(scroll, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(compact ? 10 : 20, compact ? 10 : 18,
                               compact ? 10 : 20, compact ? 10 : 18);
    layout->setSpacing(compact ? 8 : 14);
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
    m_sectionPicker->addItem(page->title());
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
