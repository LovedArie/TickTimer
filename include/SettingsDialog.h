#pragma once
// ---------------------------------------------------------------------------
// SettingsDialog — the app's one preferences surface (⚙ in the nav rail).
//
// v26.1: this class used to BE the settings — one 579-line constructor, one
// flat QFormLayout, bold labels pretending to be section headers. It is now
// a SHELL: a nav list, a page stack, and an OK button. Everything it used to
// know lives in SettingsPages.h.
//
// What the shell owns, and nothing more:
//   - the pages, in order
//   - keeping the nav selection and the visible page in sync
//   - the OK/Cancel contract
//
// Scope discipline is unchanged and still worth stating: this dialog holds
// preferences with NO natural home elsewhere. The Pomodoro keeps its
// durations on its own page because that's where you think about them. A
// nav rail makes it *easier* to hoard, so the rule matters more now, not
// less — pages are cheap, but a preference that belongs next to its feature
// still belongs next to its feature.
//
// Persistence model, unchanged: widgets edit local state, OK writes
// QSettings once, Cancel writes nothing. It is now spread across pages and
// still happens exactly once, because save() walks the pages in one pass.
// Nothing here touches AppData — closing this dialog can't dirty the planner
// or trigger a sync. The caller (MainWindow) re-applies prefs to the pages
// after accept(); the dialog doesn't know who listens, which keeps it
// reusable and dumb.
//
// WHY NOT QTabWidget: tabs stop scaling past roughly six sections — the
// labels compress, then elide, then sprout scroll arrows. A vertical list
// has room for a dozen entries and reads top-to-bottom like the settings
// screen of every app built in the last decade.
//
// CONSIDERED AND REJECTED — remembering the last page you visited. It would
// mean writing to QSettings when the dialog closes, including on Cancel,
// which quietly breaks the one promise this dialog makes out loud. A
// nice-to-have is not worth an asterisk on "Cancel writes nothing".
// ---------------------------------------------------------------------------

#include <QDialog>
#include <QVector>

class SettingsPage;
class QComboBox;
class QListWidget;
class QStackedWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    // memoryPath: which account's memory file the Memory page edits. Empty
    // means the global file — the same convention JsonStore::filePathForUser()
    // uses for an empty username, so tests and a login-less build are
    // unaffected. Defaulted rather than required because every existing call
    // site is a test that has no account and wants none.
    explicit SettingsDialog(QWidget* parent = nullptr,
                            QString memoryPath = QString());

private:
    // Append a page: one nav row, one stack entry, one vector slot — kept at
    // matching indices, which is what lets the nav drive the stack with a
    // plain signal-to-slot connection and no glue code.
    void addPage(SettingsPage* page);

    // The ONE QSettings write, on OK: ask every page to save itself.
    void save();

    QString m_memoryPath;

    // Two switchers, one job: the column on a desktop, the picker on a
    // phone. Both always exist; exactly one is ever visible.
    QListWidget*           m_nav   = nullptr;
    QComboBox*             m_sectionPicker = nullptr;
    QStackedWidget*        m_stack = nullptr;
    QVector<SettingsPage*> m_pages;
};
