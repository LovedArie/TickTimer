#pragma once
// ---------------------------------------------------------------------------
// EventDialog — the card that opens when you click a planned block:
// reschedule buttons, the plan-vs-actual bar, the Focus/Break/Stop
// controls (UC2's user-facing face), a note field, and delete.
//
// The dialog is a THIN shell: every button forwards to AppData or
// TrackerService and then the dialog re-reads reality in refresh().
// It holds no truth of its own — so it can never disagree with the data.
//
// Closing the dialog does NOT stop the timer (deliberately): TrackerService
// is owned by MainWindow and outlives any window. You close the card, go
// live your block, and the mini bar keeps growing on the agenda.
// ---------------------------------------------------------------------------

#include <QDialog>

class AppData;
class TrackerService;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class PvaBar;

class EventDialog : public QDialog
{
    Q_OBJECT

public:
    EventDialog(AppData* data, TrackerService* tracker,
                const QString& eventId, QWidget* parent = nullptr);

private slots:
    void refresh();       // re-read the event and repaint everything
    void moveBySlots(int deltaSlots);
    void saveNote();
    void saveTitle();     // the label painted on the block
    void linkTaskFromCompleter(const class QModelIndex& index);
    void unlinkTask();
    void deleteEvent();

private:
    AppData*        m_data;
    TrackerService* m_tracker;
    QString m_eventId;

    QLabel* m_swatch      = nullptr;
    QLabel* m_title       = nullptr;
    QLabel* m_plannedLine = nullptr;
    QLabel* m_stateLabel  = nullptr;
    QLabel* m_legend      = nullptr;
    PvaBar* m_pva         = nullptr;
    class LabelEdit* m_titleEdit = nullptr; // multiline label shown ON the block
    QLabel*      m_taskLine  = nullptr;     // "Linked task: Lab 4 · due Aug 8"
    QPushButton* m_unlinkBtn = nullptr;
    QPlainTextEdit* m_note = nullptr;
    QPushButton* m_focusBtn = nullptr;
    QPushButton* m_breakBtn = nullptr;
    QPushButton* m_distractedBtn = nullptr;
    QPushButton* m_stopBtn  = nullptr;
    QVector<QPushButton*> m_moveButtons; // ▲1h ▲30m 30m▼ 1h▼
    bool m_updatingUi = false; // guards against signal feedback loops
};

// The wide plan-vs-actual bar: focused + break time filling the planned
// duration. Untracked time stays visible as empty track — an honest gap,
// not an accusation.
class PvaBar : public QWidget
{
public:
    explicit PvaBar(QWidget* parent = nullptr);
    void setValues(qint64 focusSecs, qint64 breakSecs, qint64 distractedSecs,
                   qint64 plannedSecs);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override { return {320, 20}; }

private:
    qint64 m_focus = 0, m_break = 0, m_distracted = 0, m_planned = 1;
};
