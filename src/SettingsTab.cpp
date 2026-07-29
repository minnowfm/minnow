#include "SettingsTab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

SettingsTab::SettingsTab(QWidget *parent)
    : QWidget(parent)
{
    QSettings settings;

    auto *heading = new QLabel(tr("Settings"), this);
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 4);
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto *form = new QFormLayout;
    form->setSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_hiddenFilesCheck = new QCheckBox(this);
    m_hiddenFilesCheck->setChecked(settings.value(QStringLiteral("View/ShowHiddenFiles"), false).toBool());
    form->addRow(tr("Show hidden files:"), m_hiddenFilesCheck);
    connect(m_hiddenFilesCheck, &QCheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("View/ShowHiddenFiles"), checked);
    });
    connect(m_hiddenFilesCheck, &QCheckBox::toggled, this, &SettingsTab::showHiddenFilesChanged);

    m_thumbnailsCheck = new QCheckBox(this);
    m_thumbnailsCheck->setChecked(settings.value(QStringLiteral("View/ShowThumbnails"), true).toBool());
    form->addRow(tr("Show thumbnails:"), m_thumbnailsCheck);
    connect(m_thumbnailsCheck, &QCheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("View/ShowThumbnails"), checked);
    });
    connect(m_thumbnailsCheck, &QCheckBox::toggled, this, &SettingsTab::showThumbnailsChanged);

    m_iconSizeCombo = new QComboBox(this);
    static const QList<QPair<QString, int>> sizes = {
        {tr("Small"), 32},
        {tr("Medium"), 48},
        {tr("Large"), 64},
        {tr("Huge"), 96},
    };
    const int currentSize = settings.value(QStringLiteral("View/IconSize"), 64).toInt();
    int currentComboIndex = 0;
    for (const auto &entry : sizes) {
        m_iconSizeCombo->addItem(entry.first, entry.second);
        if (entry.second == currentSize)
            currentComboIndex = m_iconSizeCombo->count() - 1;
    }
    m_iconSizeCombo->setCurrentIndex(currentComboIndex);
    form->addRow(tr("Default icon size:"), m_iconSizeCombo);
    connect(m_iconSizeCombo, &QComboBox::currentIndexChanged, this, [this] {
        const int size = m_iconSizeCombo->currentData().toInt();
        QSettings settings;
        settings.setValue(QStringLiteral("View/IconSize"), size);
        Q_EMIT iconSizeChanged(size);
    });

    m_terminalCombo = new QComboBox(this);
    m_terminalCombo->setEditable(true);
    m_terminalCombo->addItem(tr("Auto-detect (recommended)"), QString());
    for (const QString &name : {QStringLiteral("konsole"), QStringLiteral("gnome-terminal"), QStringLiteral("xterm"),
                                 QStringLiteral("alacritty"), QStringLiteral("kitty"), QStringLiteral("foot"),
                                 QStringLiteral("wezterm"), QStringLiteral("tilix")})
        m_terminalCombo->addItem(name, name);
    const QString preferredTerminal = settings.value(QStringLiteral("Terminal/Command")).toString();
    if (preferredTerminal.isEmpty())
        m_terminalCombo->setCurrentIndex(0);
    else
        m_terminalCombo->setCurrentText(preferredTerminal);
    form->addRow(tr("Terminal:"), m_terminalCombo);
    connect(m_terminalCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        QSettings settings;
        // can't trust currentIndex() - typing over "Auto-detect" in place leaves index at 0
        const QString value = text == m_terminalCombo->itemText(0) ? QString() : text;
        settings.setValue(QStringLiteral("Terminal/Command"), value);
    });

    m_confirmDeleteCheck = new QCheckBox(this);
    m_confirmDeleteCheck->setChecked(settings.value(QStringLiteral("Confirmations/ConfirmPermanentDelete"), true).toBool());
    form->addRow(tr("Confirm permanent deletion:"), m_confirmDeleteCheck);
    connect(m_confirmDeleteCheck, &QCheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("Confirmations/ConfirmPermanentDelete"), checked);
    });

    // Muted secondary text color, same idea as MainWindow's footer text - palette(mid) sits
    // too close to the card background in some themes to read as intentionally dimmed.
    const QColor windowColor = palette().color(QPalette::Window);
    const QColor mutedColor = windowColor.lightness() < 128 ? QColor(190, 190, 190) : QColor(90, 90, 90);

    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);

    auto *aboutHeading = new QLabel(tr("About"), this);
    aboutHeading->setFont(headingFont);

    auto *aboutLabel = new QLabel(this);
    aboutLabel->setTextFormat(Qt::RichText);
    aboutLabel->setOpenExternalLinks(true);
    aboutLabel->setWordWrap(true);
    aboutLabel->setText(tr("<b>Minnow</b> %1<br>"
                            "<span style=\"color:%2\">A simple, lightweight file manager for KDE</span><br><br>"
                            "<span style=\"color:%2\">Created by Voten641<br>"
                            "Licensed under <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GPL-3.0-or-later</a><br>"
                            "<a href=\"https://github.com/minnowfm/minnow\">github.com/minnowfm/minnow</a></span>")
                             .arg(QStringLiteral(MINNOW_VERSION), mutedColor.name()));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addWidget(heading);
    layout->addSpacing(12);
    layout->addLayout(form);
    layout->addStretch(1);
    layout->addWidget(divider);
    layout->addSpacing(16);
    layout->addWidget(aboutHeading);
    layout->addSpacing(12);
    layout->addWidget(aboutLabel);
}
