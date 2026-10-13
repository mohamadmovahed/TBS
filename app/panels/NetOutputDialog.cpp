#include "NetOutputDialog.h"

#include "../Theme.h"
#include "FormHelpers.h"

#include <tbsdvb/Sink.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace tbs;

NetOutputDialog::NetOutputDialog(const TsNetworkOutput &o, const QVector<ServiceChoice> &services, bool bbMode,
                                 QWidget *parent)
    : QDialog(parent)
    , m_bbMode(bbMode)
{
    setWindowTitle(tr("Network output"));
    setMinimumWidth(460);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(6);

    // ---- Destination ----
    root->addWidget(Theme::sectionTitle(tr("Destination")));
    auto *dest = makeForm();
    m_protocol = new QComboBox;
    m_protocol->addItem(tr("UDP"), static_cast<int>(NetProtocol::Udp));
    m_protocol->addItem(tr("RTP (payload type 33)"), static_cast<int>(NetProtocol::Rtp));
    m_protocol->addItem(TsNetworkSink::srtAvailable() ? tr("SRT") : tr("SRT (not in this build)"),
                        static_cast<int>(NetProtocol::Srt));
    dest->addRow(keyLabel(tr("Protocol")), m_protocol);
    m_srtMode = new QComboBox;
    m_srtMode->addItem(tr("Caller (connect to a receiver)"), static_cast<int>(SrtMode::Caller));
    m_srtMode->addItem(tr("Listener (receivers connect here)"), static_cast<int>(SrtMode::Listener));
    m_host = new QLineEdit;
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setButtonSymbols(QAbstractSpinBox::NoButtons);
    auto *hostRow = new QHBoxLayout;
    hostRow->addWidget(m_host, 1);
    hostRow->addWidget(keyLabel(tr("Port")));
    hostRow->addWidget(m_port);
    m_hostLabel = keyLabel(tr("Address"));
    dest->addRow(m_hostLabel, hostRow);
    root->addLayout(dest);

    m_udpBox = new QWidget;
    auto *udp = makeForm();
    m_udpBox->setLayout(udp);
    m_ttl = new QSpinBox;
    m_ttl->setRange(1, 255);
    m_ttl->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_ttl->setToolTip(tr("Multicast TTL: how many routers the packets may cross"));
    udp->addRow(keyLabel(tr("TTL")), m_ttl);
    m_iface = new QLineEdit;
    m_iface->setPlaceholderText(tr("default route"));
    m_iface->setToolTip(tr("Multicast output interface: its IPv4 address, or its name (Linux, IPv6)"));
    udp->addRow(keyLabel(tr("Interface")), m_iface);
    root->addWidget(m_udpBox);

    m_srtBox = new QWidget;
    auto *srt = makeForm();
    m_srtBox->setLayout(srt);
    srt->addRow(keyLabel(tr("Mode")), m_srtMode);
    m_latency = new QSpinBox;
    m_latency->setRange(20, 60000);
    m_latency->setSuffix(tr(" ms"));
    m_latency->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_latency->setToolTip(tr("Receiver buffer for retransmissions; about 4 × the round-trip time"));
    srt->addRow(keyLabel(tr("Latency")), m_latency);
    m_passphrase = new QLineEdit;
    m_passphrase->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_passphrase->setPlaceholderText(tr("no encryption (10–79 characters for AES)"));
    srt->addRow(keyLabel(tr("Passphrase")), m_passphrase);
    m_streamId = new QLineEdit;
    m_streamId->setPlaceholderText(tr("none"));
    srt->addRow(keyLabel(tr("Stream ID")), m_streamId);
    root->addWidget(m_srtBox);

    // ---- Content ----
    root->addWidget(Theme::sectionTitle(tr("Content")));
    auto *content = makeForm();
    m_service = new QComboBox;
    m_service->setEditable(true);
    m_service->setInsertPolicy(QComboBox::NoInsert);
    m_service->addItem(tr("Whole transport stream"), QVariantList{0, -1});
    for (const ServiceChoice &s : services) {
        QString label = QStringLiteral("%1 · %2").arg(s.programNumber).arg(s.name.isEmpty() ? tr("(no name)") : s.name);
        if (bbMode && s.isi >= 0)
            label += tr(" · ISI %1").arg(s.isi);
        m_service->addItem(label, QVariantList{s.programNumber, s.isi});
    }
    m_service->setToolTip(tr("Pick a service from the stream, or type its service ID (program number)"));
    content->addRow(keyLabel(tr("Service")), m_service);
    m_pids = new QLineEdit;
    m_pids->setPlaceholderText(tr("none"));
    m_pids->setToolTip(tr("Comma separated, decimal or 0x hex. Without a service, only these PIDs are sent; with a "
                          "service they are added to it (e.g. 0x12 for the EIT)."));
    content->addRow(keyLabel(tr("PIDs")), m_pids);
    m_isi = new QSpinBox;
    m_isi->setRange(-1, 255);
    m_isi->setSpecialValueText(tr("first transport stream"));
    m_isi->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_isiLabel = keyLabel(tr("Input stream"));
    content->addRow(m_isiLabel, m_isi);
    m_perDatagram = new QSpinBox;
    m_perDatagram->setRange(1, 7);
    m_perDatagram->setSuffix(tr(" TS packets per datagram"));
    m_perDatagram->setButtonSymbols(QAbstractSpinBox::NoButtons);
    content->addRow(keyLabel(tr("Packing")), m_perDatagram);
    m_keepNull = new QCheckBox(tr("Keep null packets (constant bitrate)"));
    content->addRow(QString(), m_keepNull);
    root->addLayout(content);
    if (!bbMode) {
        m_isi->setVisible(false);
        m_isiLabel->setVisible(false);
    }

    m_url = hintLabel();
    m_url->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_url);
    m_error = hintLabel();
    m_error->setStyleSheet(QStringLiteral("color: %1").arg(Theme::tokens().critical.name()));
    root->addWidget(m_error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("Primary"));
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        TsNetworkOutput tmp;
        QString err;
        if (collect(&tmp, &err))
            accept();
        else
            m_error->setText(err);
    });

    // Same label column in the three forms
    for (QFormLayout *f : {dest, udp, srt, content})
        for (int i = 0; i < f->rowCount(); ++i)
            if (QLayoutItem *li = f->itemAt(i, QFormLayout::LabelRole))
                if (li->widget())
                    li->widget()->setMinimumWidth(96);

    // Initial values
    m_protocol->setCurrentIndex(m_protocol->findData(static_cast<int>(o.protocol)));
    m_host->setText(QString::fromStdString(o.host));
    m_port->setValue(o.port);
    m_ttl->setValue(o.ttl);
    m_iface->setText(QString::fromStdString(o.interfaceName));
    m_srtMode->setCurrentIndex(m_srtMode->findData(static_cast<int>(o.srtMode)));
    m_latency->setValue(o.srtLatencyMs);
    m_passphrase->setText(QString::fromStdString(o.srtPassphrase));
    m_streamId->setText(QString::fromStdString(o.srtStreamId));
    if (o.programNumber > 0) {
        int idx = -1;
        for (int i = 1; i < m_service->count(); ++i)
            if (m_service->itemData(i).toList().value(0).toInt() == o.programNumber
                && (o.isi < 0 || m_service->itemData(i).toList().value(1).toInt() == o.isi))
                idx = i;
        if (idx >= 0)
            m_service->setCurrentIndex(idx);
        else
            m_service->setEditText(QString::number(o.programNumber));
    } else {
        m_service->setCurrentIndex(0);
    }
    QStringList pids;
    for (uint16_t p : o.pids)
        pids << QStringLiteral("0x%1").arg(p, 4, 16, QLatin1Char('0'));
    m_pids->setText(pids.join(QStringLiteral(", ")));
    m_isi->setValue(o.isi);
    m_perDatagram->setValue(o.packetsPerDatagram);
    m_keepNull->setChecked(!o.dropNullPackets);

    connect(m_protocol, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        updateVisibility();
        updateUrl();
    });
    connect(m_srtMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        updateVisibility();
        updateUrl();
    });
    connect(m_service, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        const int isi = m_service->itemData(i).toList().value(1).toInt();
        if (m_bbMode && isi >= 0)
            m_isi->setValue(isi);
        updateUrl();
    });
    for (QLineEdit *e : {m_host, m_iface, m_passphrase, m_streamId, m_pids, m_service->lineEdit()})
        connect(e, &QLineEdit::textChanged, this, &NetOutputDialog::updateUrl);
    for (QSpinBox *sp : {m_port, m_ttl, m_latency, m_isi, m_perDatagram})
        connect(sp, qOverload<int>(&QSpinBox::valueChanged), this, &NetOutputDialog::updateUrl);
    connect(m_keepNull, &QCheckBox::toggled, this, &NetOutputDialog::updateUrl);
    updateVisibility();
    updateUrl();
}

void NetOutputDialog::updateVisibility()
{
    const bool srt = static_cast<NetProtocol>(m_protocol->currentData().toInt()) == NetProtocol::Srt;
    const bool listener = static_cast<SrtMode>(m_srtMode->currentData().toInt()) == SrtMode::Listener;
    m_udpBox->setVisible(!srt);
    m_srtBox->setVisible(srt);
    m_hostLabel->setText(srt && listener ? tr("Listen on") : tr("Address"));
    m_host->setPlaceholderText(srt && listener ? tr("all interfaces") : tr("239.1.1.1 or host name"));
    m_streamId->setEnabled(!listener);
    adjustSize();
}

bool NetOutputDialog::collect(TsNetworkOutput *o, QString *error) const
{
    o->protocol = static_cast<NetProtocol>(m_protocol->currentData().toInt());
    o->host = m_host->text().trimmed().toStdString();
    o->port = static_cast<uint16_t>(m_port->value());
    o->ttl = m_ttl->value();
    o->interfaceName = m_iface->text().trimmed().toStdString();
    o->srtMode = static_cast<SrtMode>(m_srtMode->currentData().toInt());
    o->srtLatencyMs = m_latency->value();
    o->srtPassphrase = m_passphrase->text().toStdString();
    o->srtStreamId = m_streamId->text().trimmed().toStdString();
    o->isi = m_bbMode ? m_isi->value() : -1;
    o->packetsPerDatagram = m_perDatagram->value();
    o->dropNullPackets = !m_keepNull->isChecked();

    // Service: a listed entry, or a typed number
    o->programNumber = 0;
    const int idx = m_service->findText(m_service->currentText());
    if (idx > 0) {
        o->programNumber = m_service->itemData(idx).toList().value(0).toInt();
    } else if (idx < 0) {
        const QString t = m_service->currentText().trimmed().section(QLatin1Char(' '), 0, 0);
        bool ok = false;
        const int v = t.toInt(&ok, 0);
        if (!ok || v < 1 || v > 65535) {
            *error = tr("Service: pick one from the list or enter a service ID (1–65535).");
            return false;
        }
        o->programNumber = v;
    }

    o->pids.clear();
    const QStringList parts = m_pids->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        bool ok = false;
        const int v = p.toInt(&ok, 0);
        if (!ok || v < 0 || v > 0x1FFF) {
            *error = tr("Invalid PID \"%1\" (0–8191, or 0x0000–0x1FFF).").arg(p);
            return false;
        }
        o->pids.push_back(static_cast<uint16_t>(v));
    }

    if (o->protocol == NetProtocol::Srt && !TsNetworkSink::srtAvailable()) {
        *error = tr("This build has no SRT support.");
        return false;
    }
    if (o->host.empty() && !(o->protocol == NetProtocol::Srt && o->srtMode == SrtMode::Listener)) {
        *error = tr("Enter the destination address.");
        return false;
    }
    if (o->protocol == NetProtocol::Srt && !o->srtPassphrase.empty()
        && (o->srtPassphrase.size() < 10 || o->srtPassphrase.size() > 79)) {
        *error = tr("The SRT passphrase must have 10 to 79 characters.");
        return false;
    }
    return true;
}

void NetOutputDialog::updateUrl()
{
    TsNetworkOutput o;
    QString err;
    if (collect(&o, &err)) {
        m_error->clear();
        m_url->setText(tr("URL: <tt>%1</tt>").arg(QString::fromStdString(networkUrl(o, true)).toHtmlEscaped()));
    } else {
        m_url->clear();
    }
}

TsNetworkOutput NetOutputDialog::output() const
{
    TsNetworkOutput o;
    QString err;
    collect(&o, &err);
    return o;
}
