#include "UpdateBanner.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QUrl>

UpdateBanner::UpdateBanner(const QString& latestVersion, const QString& url,
                           const QString& notes, QWidget* parent)
    : QWidget(parent)
{
    setObjectName("updateBanner"); // styled in Theme.h

    auto* text = new QLabel(
        notes.trimmed().isEmpty()
            ? tr("TickTimer %1 is available.").arg(latestVersion)
            : tr("TickTimer %1 is available — %2").arg(latestVersion,
                                                       notes.trimmed()),
        this);
    text->setWordWrap(true);

    auto* get = new QPushButton(tr("Get it"), this);
    get->setObjectName("primary");
    get->setCursor(Qt::PointingHandCursor);

    auto* dismiss = new QPushButton(QStringLiteral("✕"), this);
    dismiss->setObjectName("quiet");
    dismiss->setFixedWidth(28);
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setToolTip(tr("Not now — don't mention this version again"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 8, 8, 8);
    layout->addWidget(text, 1);
    layout->addWidget(get);
    layout->addWidget(dismiss);

    connect(get, &QPushButton::clicked, this, [url]() {
        // Hand the URL to the system browser and step aside. The app's
        // Level-1 contract: it TELLS you about updates, it never touches
        // its own files (a running exe can't overwrite itself on Windows
        // anyway — the addendum's §B explains why we didn't fight that).
        QDesktopServices::openUrl(QUrl(url));
    });

    connect(dismiss, &QPushButton::clicked, this, [this, latestVersion]() {
        // Remember WHICH version was waved away. decideBanner reads this on
        // every future launch: 19.0.0 dismissed stays quiet; 19.0.1 speaks.
        QSettings().setValue(QStringLiteral("update/lastDismissed"),
                             latestVersion);
        hide();
    });
}
