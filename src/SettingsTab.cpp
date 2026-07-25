#include "SettingsTab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
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
        const QString value = m_terminalCombo->currentIndex() == 0 ? QString() : text;
        settings.setValue(QStringLiteral("Terminal/Command"), value);
    });

    m_confirmDeleteCheck = new QCheckBox(this);
    m_confirmDeleteCheck->setChecked(settings.value(QStringLiteral("Confirmations/ConfirmPermanentDelete"), true).toBool());
    form->addRow(tr("Confirm permanent deletion:"), m_confirmDeleteCheck);
    connect(m_confirmDeleteCheck, &QCheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("Confirmations/ConfirmPermanentDelete"), checked);
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addWidget(heading);
    layout->addSpacing(12);
    layout->addLayout(form);
    layout->addStretch(1);
}
