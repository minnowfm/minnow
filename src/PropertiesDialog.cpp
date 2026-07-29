#include "PropertiesDialog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QToolButton>
#include <QVBoxLayout>

#include <KApplicationTrader>
#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/PropertyInfo>
#include <KFileMetaData/SimpleExtractionResult>
#include <KIO/DirectorySizeJob>
#include <KIO/Global>
#include <KJob>
#include <KService>

namespace
{
const QColor kGood(0x3F, 0xAE, 0x7A); // free-space gauge only, stays green regardless of accent color

QString rgba(const QColor &c, int alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// flat tinted plate behind the file's real icon, no gradients
class IconPlate : public QWidget
{
public:
    IconPlate(const QIcon &icon, const QColor &tint, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_icon(icon)
        , m_tint(tint)
    {
        setFixedSize(52, 52);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor bg = m_tint;
        bg.setAlphaF(0.16f);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(rect(), 14, 14);

        const QSize iconSize(28, 28);
        const QRect iconRect(QPoint((width() - iconSize.width()) / 2, (height() - iconSize.height()) / 2), iconSize);
        m_icon.paint(&painter, iconRect);
    }

private:
    QIcon m_icon;
    QColor m_tint;
};

// two concentric arcs - track + fill for the used fraction
class RadialGauge : public QWidget
{
public:
    explicit RadialGauge(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(52, 52);
    }

    void setFraction(double usedFraction, const QColor &track, const QColor &fill)
    {
        m_fraction = qBound(0.0, usedFraction, 1.0);
        m_track = track;
        m_fill = fill;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int pad = 4;
        const QRectF arcRect(pad, pad, width() - 2 * pad, height() - 2 * pad);

        QPen trackPen(m_track, 4, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(trackPen);
        painter.drawArc(arcRect, 0, 360 * 16);

        if (m_fraction > 0.002) {
            QPen fillPen(m_fill, 4, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(fillPen);
            const int span = qMax(1, int(m_fraction * 360 * 16));
            painter.drawArc(arcRect, 90 * 16, -span);
        }
    }

private:
    double m_fraction = 0.0;
    QColor m_track;
    QColor m_fill;
};

QFrame *makeTile(const QString &label, QLabel **valueOut, QWidget *parent)
{
    auto *tile = new QFrame(parent);
    tile->setObjectName(QStringLiteral("propTile"));
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(3);

    auto *labelWidget = new QLabel(label.toUpper(), tile);
    labelWidget->setObjectName(QStringLiteral("propTileLabel"));

    auto *valueWidget = new QLabel(QStringLiteral("—"), tile);
    valueWidget->setObjectName(QStringLiteral("propTileValue"));

    layout->addWidget(labelWidget);
    layout->addWidget(valueWidget);
    if (valueOut)
        *valueOut = valueWidget;
    return tile;
}

QFrame *makePanel(const QString &title, QVBoxLayout **bodyOut, QWidget *parent)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("propPanel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);

    if (!title.isEmpty()) {
        auto *titleLabel = new QLabel(title.toUpper(), panel);
        titleLabel->setObjectName(QStringLiteral("propPanelTitle"));
        layout->addWidget(titleLabel);
    }

    auto *body = new QVBoxLayout;
    body->setSpacing(5);
    layout->addLayout(body);
    if (bodyOut)
        *bodyOut = body;
    return panel;
}

QLabel *addRow(QVBoxLayout *body, const QString &key, const QString &value, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);

    auto *keyLabel = new QLabel(key, row);
    keyLabel->setObjectName(QStringLiteral("propRowKey"));

    auto *valueLabel = new QLabel(value, row);
    valueLabel->setObjectName(QStringLiteral("propRowValue"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setWordWrap(true);

    rowLayout->addWidget(keyLabel);
    rowLayout->addWidget(valueLabel, 1);
    body->addWidget(row);
    return valueLabel;
}

QString formatFullTime(const QDateTime &dt)
{
    return dt.isValid() ? dt.toString(QStringLiteral("d MMM yyyy, HH:mm")) : QObject::tr("Unknown");
}

QString formatShortTime(const QDateTime &dt)
{
    return dt.isValid() ? dt.toString(QStringLiteral("HH:mm")) : QStringLiteral("—");
}
}

PropertiesDialog::PropertiesDialog(const KFileItemList &items, QWidget *parent)
    : QDialog(parent)
    , m_items(items)
{
    if (m_items.size() == 1)
        m_item = m_items.first();

    setWindowTitle(m_items.size() == 1 ? tr("Properties — %1").arg(m_item.name()) : tr("Properties"));
    setMinimumWidth(440);

    const QColor windowColor = palette().color(QPalette::Window);
    const bool dark = windowColor.lightness() < 128;
    const QColor cardBg = dark ? windowColor.lighter(130) : windowColor.darker(103);
    const QColor cardBorder = dark ? windowColor.lighter(160) : windowColor.darker(115);
    const QColor mutedText = dark ? QColor(160, 160, 165) : QColor(110, 110, 116);
    m_accent = palette().color(QPalette::Highlight);

    setStyleSheet(QStringLiteral(
                      "QFrame#propTile, QFrame#propPanel { background: %1; border: 1px solid %2; border-radius: 12px; }"
                      "QLabel#propTileLabel, QLabel#propPanelTitle { font-size: 10px; font-weight: 600; letter-spacing: 0.5px; color: %3; }"
                      "QLabel#propTileValue { font-size: 14px; font-weight: 700; }"
                      "QLabel#propRowKey { color: %3; font-size: 12px; }"
                      "QLabel#propRowValue { font-size: 12px; }"
                      "QLabel#propGaugeText { color: %3; font-size: 11px; }"
                      "QLabel#propBadgeOn { background: %4; border-radius: 5px; "
                      "font-size: 10px; font-weight: 700; color: %5; }"
                      "QLabel#propBadgeOff { background: transparent; border: 1px solid %2; border-radius: 5px; "
                      "font-size: 10px; font-weight: 700; color: %2; }"
                      "QToolButton#propRailBtn { border: none; border-radius: 9px; }"
                      "QToolButton#propRailBtn:checked { background: %6; }")
                      .arg(cardBg.name(), cardBorder.name(), mutedText.name(), rgba(m_accent, 60), m_accent.name(),
                           rgba(m_accent, 45)));

    if (m_items.size() == 1 && !m_item.isDir())
        extractMediaProperties();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *content = new QWidget(this);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    const bool isSingle = m_items.size() == 1;
    const bool isSingleDir = isSingle && m_item.isDir();

    if (isSingle) {
        auto *rail = new QWidget(content);
        rail->setFixedWidth(56);
        auto *railLayout = new QVBoxLayout(rail);
        railLayout->setContentsMargins(0, 16, 0, 16);
        railLayout->setSpacing(6);
        railLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

        QStringList iconNames = {QStringLiteral("documentinfo"), QStringLiteral("object-locked")};
        if (!m_mediaProps.isEmpty())
            iconNames << QStringLiteral("view-media-visualization");
        if (isSingleDir)
            iconNames << QStringLiteral("folder-open");

        auto *group = new QButtonGroup(this);
        int idx = 0;
        for (const QString &iconName : std::as_const(iconNames)) {
            auto *btn = new QToolButton(rail);
            btn->setObjectName(QStringLiteral("propRailBtn"));
            btn->setCheckable(true);
            btn->setAutoRaise(true);
            btn->setFixedSize(34, 34);
            btn->setIconSize(QSize(16, 16));
            btn->setIcon(QIcon::fromTheme(iconName));
            group->addButton(btn, idx);
            railLayout->addWidget(btn);
            ++idx;
        }
        group->button(0)->setChecked(true);
        connect(group, &QButtonGroup::idClicked, this, [this](int id) { m_stack->setCurrentIndex(id); });

        contentLayout->addWidget(rail);
    }

    auto *main = new QWidget(content);
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(20, 20, 20, 16);
    mainLayout->setSpacing(16);
    mainLayout->addWidget(buildHero());

    m_stack = new QStackedWidget(main);
    m_stack->addWidget(buildGeneralPane());
    if (isSingle) {
        m_stack->addWidget(buildPermissionsPane());
        if (!m_mediaProps.isEmpty())
            m_stack->addWidget(buildMediaPane());
        if (isSingleDir)
            m_stack->addWidget(buildContentsPane());
    }
    mainLayout->addWidget(m_stack, 1);

    contentLayout->addWidget(main, 1);
    outer->addWidget(content, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *btnRow = new QWidget(this);
    auto *btnLayout = new QHBoxLayout(btnRow);
    btnLayout->setContentsMargins(20, 0, 20, 16);
    btnLayout->addWidget(buttons);
    outer->addWidget(btnRow);

    startSizeCalculation();
}

QWidget *PropertiesDialog::buildHero()
{
    auto *hero = new QWidget(this);
    auto *layout = new QHBoxLayout(hero);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    const bool isSingle = m_items.size() == 1;
    const QString iconName = isSingle ? m_item.iconName() : QStringLiteral("document-multiple");
    auto *plate = new IconPlate(QIcon::fromTheme(iconName, QIcon::fromTheme(QStringLiteral("folder"))), m_accent, hero);
    layout->addWidget(plate);

    auto *textCol = new QVBoxLayout;
    textCol->setSpacing(2);

    auto *nameLabel = new QLabel(hero);
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(nameFont.pointSize() + 3);
    nameFont.setWeight(QFont::DemiBold);
    nameLabel->setFont(nameFont);

    auto *subLabel = new QLabel(hero);
    subLabel->setObjectName(QStringLiteral("propGaugeText"));

    if (isSingle) {
        nameLabel->setText(m_item.name());
        const QString type = m_item.isDir() ? tr("Folder") : m_item.mimeComment();
        subLabel->setText(m_item.user().isEmpty() ? type : tr("%1 · owned by %2").arg(type, m_item.user()));
    } else {
        int dirs = 0, files = 0;
        for (const KFileItem &it : std::as_const(m_items))
            it.isDir() ? ++dirs : ++files;
        nameLabel->setText(tr("%n item(s) selected", "", m_items.size()));
        subLabel->setText(tr("%1 files, %2 folders").arg(files).arg(dirs));
    }

    textCol->addWidget(nameLabel);
    textCol->addWidget(subLabel);
    layout->addLayout(textCol, 1);
    return hero;
}

QWidget *PropertiesDialog::buildGeneralPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *tiles = new QWidget(pane);
    auto *tilesLayout = new QHBoxLayout(tiles);
    tilesLayout->setContentsMargins(0, 0, 0, 0);
    tilesLayout->setSpacing(8);

    tilesLayout->addWidget(makeTile(tr("Size"), &m_sizeTileValue, tiles));
    tilesLayout->addWidget(makeTile(tr("Items"), &m_itemsTileValue, tiles));
    QLabel *modifiedValue = nullptr;
    tilesLayout->addWidget(makeTile(tr("Modified"), &modifiedValue, tiles));
    layout->addWidget(tiles);

    const bool isSingle = m_items.size() == 1;

    if (isSingle) {
        modifiedValue->setText(formatShortTime(m_item.time(KFileItem::ModificationTime)));
        if (!m_item.isDir())
            m_itemsTileValue->setText(QStringLiteral("—"));

        QVBoxLayout *body = nullptr;
        auto *locationPanel = makePanel(tr("Location"), &body, pane);
        addRow(body, tr("Path"), m_item.url().adjusted(QUrl::RemoveFilename).toDisplayString(QUrl::PreferLocalFile), locationPanel);

        const KService::Ptr svc = KApplicationTrader::preferredService(m_item.mimetype());
        addRow(body, tr("Open with"), svc ? svc->name() : tr("Unknown"), locationPanel);
        addRow(body, tr("Created"), formatFullTime(m_item.time(KFileItem::CreationTime)), locationPanel);
        addRow(body, tr("Accessed"), formatFullTime(m_item.time(KFileItem::AccessTime)), locationPanel);
        layout->addWidget(locationPanel);

        if (m_item.isLocalFile()) {
            QStorageInfo info(m_item.url().toLocalFile());
            if (info.isValid() && info.bytesTotal() > 0) {
                QVBoxLayout *volBody = nullptr;
                auto *volPanel = makePanel(
                    tr("Volume · %1 on %2").arg(QString::fromUtf8(info.fileSystemType()), info.rootPath()), &volBody, pane);

                auto *gaugeRow = new QWidget(volPanel);
                auto *gaugeLayout = new QHBoxLayout(gaugeRow);
                gaugeLayout->setContentsMargins(0, 0, 0, 0);
                gaugeLayout->setSpacing(14);

                auto *gauge = new RadialGauge(gaugeRow);
                const double usedFraction = 1.0 - double(info.bytesAvailable()) / double(info.bytesTotal());
                const QColor windowColor = palette().color(QPalette::Window);
                const QColor track = windowColor.lightness() < 128 ? windowColor.lighter(170) : windowColor.darker(120);
                gauge->setFraction(usedFraction, track, kGood);
                gaugeLayout->addWidget(gauge);

                auto *gaugeText = new QLabel(tr("<b>%1</b> free of %2 total")
                                                  .arg(KIO::convertSize(info.bytesAvailable()), KIO::convertSize(info.bytesTotal())),
                                              gaugeRow);
                gaugeText->setObjectName(QStringLiteral("propGaugeText"));
                gaugeText->setWordWrap(true);
                gaugeLayout->addWidget(gaugeText, 1);

                volBody->addWidget(gaugeRow);
                layout->addWidget(volPanel);
            }
        }
    } else {
        modifiedValue->setText(QStringLiteral("—"));
        m_itemsTileValue->setText(QString::number(m_items.size()));
    }

    layout->addStretch(1);
    return pane;
}

QWidget *PropertiesDialog::buildPermissionsPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    const mode_t mode = m_item.permissions();
    auto bit = [mode](mode_t mask) { return (mode & mask) != 0; };

    QVBoxLayout *body = nullptr;
    auto *panel = makePanel(tr("Access"), &body, pane);

    auto addPermRow = [&](const QString &label, bool r, bool w, bool x) {
        auto *row = new QWidget(panel);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        rowLayout->addWidget(new QLabel(label, row), 1);

        auto *badges = new QWidget(row);
        auto *badgeLayout = new QHBoxLayout(badges);
        badgeLayout->setContentsMargins(0, 0, 0, 0);
        badgeLayout->setSpacing(4);
        const QList<QPair<QString, bool>> bits = {{QStringLiteral("R"), r}, {QStringLiteral("W"), w}, {QStringLiteral("X"), x}};
        for (const auto &b : bits) {
            auto *badge = new QLabel(b.first, badges);
            badge->setFixedSize(22, 20);
            badge->setAlignment(Qt::AlignCenter);
            badge->setObjectName(b.second ? QStringLiteral("propBadgeOn") : QStringLiteral("propBadgeOff"));
            badgeLayout->addWidget(badge);
        }
        rowLayout->addWidget(badges);
        body->addWidget(row);
    };

    addPermRow(tr("Owner · %1").arg(m_item.user()), bit(0400), bit(0200), bit(0100));
    addPermRow(tr("Group · %1").arg(m_item.group()), bit(0040), bit(0020), bit(0010));
    addPermRow(tr("Others"), bit(0004), bit(0002), bit(0001));
    layout->addWidget(panel);

    QVBoxLayout *octalBody = nullptr;
    auto *octalPanel = makePanel(QString(), &octalBody, pane);
    addRow(octalBody, tr("Octal"), QString::number(mode & 07777, 8).rightJustified(3, QLatin1Char('0')), octalPanel);
    layout->addWidget(octalPanel);

    layout->addStretch(1);
    return pane;
}

QWidget *PropertiesDialog::buildMediaPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QVBoxLayout *body = nullptr;
    auto *panel = makePanel(tr("Media"), &body, pane);

    // combine width+height into one "Dimensions" row, reads better than two separate rows
    if (m_mediaProps.contains(KFileMetaData::Property::Width) && m_mediaProps.contains(KFileMetaData::Property::Height)) {
        const int w = m_mediaProps.value(KFileMetaData::Property::Width).toInt();
        const int h = m_mediaProps.value(KFileMetaData::Property::Height).toInt();
        addRow(body, tr("Dimensions"), tr("%1 × %2").arg(w).arg(h), panel);
    }

    // priority order across image/audio/video props - anything the extractor didn't find
    // for this file just gets skipped below, no blank rows
    static const QList<KFileMetaData::Property::Property> kOrder = {
        KFileMetaData::Property::Title,
        KFileMetaData::Property::Duration,
#ifdef MINNOW_HAVE_KFILEMETADATA_CODEC_PROPS
        KFileMetaData::Property::VideoCodec,
        KFileMetaData::Property::AudioCodec,
#endif
        KFileMetaData::Property::FrameRate,
        KFileMetaData::Property::AspectRatio,
        KFileMetaData::Property::BitRate,
        KFileMetaData::Property::SampleRate,
        KFileMetaData::Property::Channels,
        KFileMetaData::Property::Artist,
        KFileMetaData::Property::Album,
        KFileMetaData::Property::AlbumArtist,
        KFileMetaData::Property::Composer,
        KFileMetaData::Property::Genre,
        KFileMetaData::Property::TrackNumber,
        KFileMetaData::Property::ReleaseYear,
        KFileMetaData::Property::Manufacturer,
        KFileMetaData::Property::Model,
        KFileMetaData::Property::ImageDateTime,
        KFileMetaData::Property::PhotoDateTimeOriginal,
        KFileMetaData::Property::PhotoFocalLength,
        KFileMetaData::Property::PhotoFocalLengthIn35mmFilm,
        KFileMetaData::Property::PhotoFNumber,
        KFileMetaData::Property::PhotoApertureValue,
        KFileMetaData::Property::PhotoExposureTime,
        KFileMetaData::Property::PhotoExposureBiasValue,
        KFileMetaData::Property::PhotoISOSpeedRatings,
        KFileMetaData::Property::PhotoFlash,
        KFileMetaData::Property::PhotoWhiteBalance,
        KFileMetaData::Property::PhotoMeteringMode,
        KFileMetaData::Property::PhotoGpsLatitude,
        KFileMetaData::Property::PhotoGpsLongitude,
        KFileMetaData::Property::PhotoGpsAltitude,
        KFileMetaData::Property::ImageOrientation,
#ifdef MINNOW_HAVE_KFILEMETADATA_CODEC_PROPS
        KFileMetaData::Property::ColorSpace,
        KFileMetaData::Property::PixelFormat,
#endif
    };

    for (const auto &prop : kOrder) {
        if (!m_mediaProps.contains(prop))
            continue;
        const KFileMetaData::PropertyInfo info(prop);
        const QString formatted = info.formatAsDisplayString(m_mediaProps.value(prop));
        if (formatted.isEmpty())
            continue;
        addRow(body, info.displayName(), formatted, panel);
    }

    layout->addWidget(panel);
    layout->addStretch(1);
    return pane;
}

void PropertiesDialog::extractMediaProperties()
{
    if (!m_item.isLocalFile())
        return;

    KFileMetaData::ExtractorCollection collection;
    QString mimetype = m_item.mimetype();
    QList<KFileMetaData::Extractor *> extractors = collection.fetchExtractors(mimetype);

    if (extractors.isEmpty()) {
        // some shared-mime-info versions detect .mkv etc as the legacy "video/matroska"
        // instead of "video/x-matroska", which is the only name the ffmpeg extractor knows -
        // try the modern name before giving up
        static const QHash<QString, QString> kMimeFallbacks = {
            {QStringLiteral("video/matroska"), QStringLiteral("video/x-matroska")},
            {QStringLiteral("audio/matroska"), QStringLiteral("audio/x-matroska")},
        };
        const auto it = kMimeFallbacks.constFind(mimetype);
        if (it != kMimeFallbacks.constEnd()) {
            mimetype = it.value();
            extractors = collection.fetchExtractors(mimetype);
        }
    }

    if (extractors.isEmpty())
        return;

    KFileMetaData::SimpleExtractionResult result(
        m_item.url().toLocalFile(), mimetype,
        KFileMetaData::ExtractionResult::Flags{KFileMetaData::ExtractionResult::ExtractMetaData});
    for (KFileMetaData::Extractor *extractor : extractors)
        extractor->extract(&result);

    m_mediaProps = result.properties();
}

QWidget *PropertiesDialog::buildContentsPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QVBoxLayout *body = nullptr;
    auto *panel = makePanel(tr("Composition"), &body, pane);
    m_contentsFiles = addRow(body, tr("Files"), tr("Calculating…"), panel);
    m_contentsSubdirs = addRow(body, tr("Sub-folders"), tr("Calculating…"), panel);
    m_contentsSize = addRow(body, tr("Total size"), tr("Calculating…"), panel);
    layout->addWidget(panel);

    layout->addStretch(1);
    return pane;
}

void PropertiesDialog::startSizeCalculation()
{
    const bool isSingle = m_items.size() == 1;

    if (isSingle && !m_item.isDir()) {
        m_sizeTileValue->setText(KIO::convertSize(m_item.size()));
        return;
    }

    m_sizeJob = isSingle ? KIO::directorySize(m_item.url()) : KIO::directorySize(m_items);
    connect(m_sizeJob, &KJob::result, this, [this](KJob *job) {
        auto *sizeJob = qobject_cast<KIO::DirectorySizeJob *>(job);
        if (sizeJob && !sizeJob->error())
            applySize(sizeJob->totalSize(), sizeJob->totalFiles(), sizeJob->totalSubdirs(), false);
        m_sizeJob = nullptr;
    });
}

void PropertiesDialog::applySize(quint64 bytes, quint64 files, quint64 subdirs, bool stillCalculating)
{
    Q_UNUSED(stillCalculating);

    if (m_sizeTileValue)
        m_sizeTileValue->setText(KIO::convertSize(bytes));
    if (m_itemsTileValue && m_items.size() == 1 && m_item.isDir())
        m_itemsTileValue->setText(QString::number(files + subdirs));

    if (m_contentsFiles)
        m_contentsFiles->setText(QString::number(files));
    if (m_contentsSubdirs)
        m_contentsSubdirs->setText(QString::number(subdirs));
    if (m_contentsSize)
        m_contentsSize->setText(KIO::convertSize(bytes));
}
