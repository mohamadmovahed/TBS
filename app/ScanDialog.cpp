#include "ScanDialog.h"

#include "PresetStore.h"
#include "Theme.h"
#include "panels/FormHelpers.h"
#include "widgets/SpectrumChart.h"

#include <tbsdvb/DvbNames.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace tbs;

namespace {

QString polLetter(Polarization p)
{
    return p == Polarization::VerticalRight13V ? QStringLiteral("V") : QStringLiteral("H");
}

QDoubleSpinBox *mhzBox(double v)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(0, 30000);
    s->setDecimals(1);
    s->setSuffix(QStringLiteral(" MHz"));
    s->setValue(v);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return s;
}

QTableWidgetItem *cell(const QString &text, bool number = false)
{
    auto *i = new QTableWidgetItem(text);
    i->setFlags(i->flags() & ~Qt::ItemIsEditable);
    if (number)
        i->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return i;
}

} // namespace

ScanDialog::ScanDialog(Hooks hooks, const QString &receiverName, QWidget *parent)
    : QDialog(parent)
    , m_hooks(std::move(hooks))
{
    setWindowTitle(tr("Scan — %1").arg(receiverName));
    resize(980, 720);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(18, 14, 18, 14);
    v->setSpacing(10);
    m_caps = hintLabel();
    v->addWidget(m_caps);
    m_tabs = new QTabWidget;
    m_tabs->addTab(buildSpectrumTab(), tr("Spectrum && blind scan"));
    m_tabs->addTab(buildNetworkTab(), tr("Network scan (NIT)"));
    v->addWidget(m_tabs, 1);
    auto *close = new QPushButton(tr("Close"));
    close->setObjectName(QStringLiteral("Ghost"));
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(close);
    v->addLayout(row);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    probeCapabilities();
}

ScanDialog::~ScanDialog()
{
    m_cancel = true;
    joinWorker();
}

void ScanDialog::closeEvent(QCloseEvent *e)
{
    if (m_busy) {
        m_cancel = true;
        m_status->setText(tr("Cancelling…"));
        m_netStatus->setText(tr("Cancelling…"));
    }
    QDialog::closeEvent(e);
}

void ScanDialog::joinWorker()
{
    if (m_worker.joinable())
        m_worker.join();
}

QWidget *ScanDialog::buildSpectrumTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);

    auto *controls = new QHBoxLayout;
    const TuningParams lnb = m_hooks.config().tuning;
    const bool ku = lnb.lnbType == LnbType::Universal;
    m_from = mhzBox(ku ? 10700 : lnb.lnbType == LnbType::Direct ? 950 : 3400);
    m_to = mhzBox(ku ? 12750 : lnb.lnbType == LnbType::Direct ? 2150 : 4200);
    m_pol = new QComboBox;
    m_pol->addItem(tr("Horizontal"), 0);
    m_pol->addItem(tr("Vertical"), 1);
    m_pol->addItem(tr("Both"), 2);
    m_pol->setCurrentIndex(lnb.polarization == Polarization::VerticalRight13V ? 1 : 0);
    m_method = new QComboBox;
    m_method->addItem(tr("FFT"), static_cast<int>(SpectrumMethod::Fft));
    m_method->addItem(tr("Sweep"), static_cast<int>(SpectrumMethod::Sweep));
    m_method->setToolTip(tr("FFT is faster; sweep works on more tuners"));
    controls->addWidget(keyLabel(tr("From")));
    controls->addWidget(m_from);
    controls->addWidget(keyLabel(tr("to")));
    controls->addWidget(m_to);
    controls->addSpacing(8);
    controls->addWidget(keyLabel(tr("Polarization")));
    controls->addWidget(m_pol);
    controls->addWidget(keyLabel(tr("Method")));
    controls->addWidget(m_method);
    controls->addStretch();
    m_spectrumButton = new QPushButton(tr("Measure spectrum"));
    m_blindButton = new QPushButton(tr("Blind scan"));
    m_blindButton->setObjectName(QStringLiteral("Primary"));
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setObjectName(QStringLiteral("Ghost"));
    m_cancelButton->setEnabled(false);
    controls->addWidget(m_spectrumButton);
    controls->addWidget(m_blindButton);
    controls->addWidget(m_cancelButton);
    v->addLayout(controls);

    auto *statusRow = new QHBoxLayout;
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_progress->setFixedWidth(180);
    m_progress->setVisible(false);
    m_status = new QLabel(tr("Measures the RF level across the band and finds the carriers in it."));
    m_status->setObjectName(QStringLiteral("Muted"));
    m_showPol = new QComboBox;
    m_showPol->setVisible(false);
    statusRow->addWidget(m_progress);
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_showPol);
    v->addLayout(statusRow);

    m_chart = new SpectrumChart;
    m_chart->setPlaceholder(tr("No spectrum yet.\nSpectrum and blind scan need the neumoDVB driver (see README)."));
    v->addWidget(m_chart, 3);

    m_carriers = new QTableWidget(0, 8);
    m_carriers->setHorizontalHeaderLabels({tr("Frequency"), tr("Pol"), tr("Symbol rate"), tr("Standard"), tr("Modulation"),
                                           tr("C/N"), tr("Streams"), tr("Source")});
    m_carriers->verticalHeader()->hide();
    m_carriers->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_carriers->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_carriers->horizontalHeader()->setStretchLastSection(true);
    m_carriers->setAlternatingRowColors(true);
    v->addWidget(m_carriers, 2);

    auto *actions = new QHBoxLayout;
    m_tuneButton = new QPushButton(tr("Tune"));
    m_saveButton = new QPushButton(tr("Save as preset…"));
    m_saveAllButton = new QPushButton(tr("Save all as presets"));
    m_csvButton = new QPushButton(tr("Export CSV…"));
    for (QPushButton *b : {m_saveButton, m_saveAllButton, m_csvButton})
        b->setObjectName(QStringLiteral("Ghost"));
    actions->addWidget(m_tuneButton);
    actions->addWidget(m_saveButton);
    actions->addWidget(m_saveAllButton);
    actions->addStretch();
    actions->addWidget(m_csvButton);
    v->addLayout(actions);
    for (QPushButton *b : {m_tuneButton, m_saveButton, m_saveAllButton, m_csvButton})
        b->setEnabled(false);

    connect(m_spectrumButton, &QPushButton::clicked, this, [this]() { startFrontendScan(false); });
    connect(m_blindButton, &QPushButton::clicked, this, [this]() { startFrontendScan(true); });
    connect(m_cancelButton, &QPushButton::clicked, this, &ScanDialog::cancel);
    connect(m_showPol, qOverload<int>(&QComboBox::currentIndexChanged), this, &ScanDialog::showPolarization);
    connect(m_chart, &SpectrumChart::markerClicked, this, [this](int i) {
        if (i < m_carriers->rowCount())
            m_carriers->selectRow(i);
    });
    connect(m_chart, &SpectrumChart::markerActivated, this, [this](int i) {
        m_carriers->selectRow(i);
        emit tuneRequested(carrierTuning(i));
    });
    connect(m_carriers, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = selectedCarrier();
        m_chart->setSelected(row);
        m_tuneButton->setEnabled(row >= 0 && !m_busy);
        m_saveButton->setEnabled(row >= 0);
    });
    connect(m_carriers, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { emit tuneRequested(carrierTuning(row)); });
    connect(m_tuneButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedCarrier();
        if (row >= 0)
            emit tuneRequested(carrierTuning(row));
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this]() { saveCarriers(false); });
    connect(m_saveAllButton, &QPushButton::clicked, this, [this]() { saveCarriers(true); });
    connect(m_csvButton, &QPushButton::clicked, this, &ScanDialog::exportCsv);
    return w;
}

QWidget *ScanDialog::buildNetworkTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);
    auto *intro = hintLabel(tr("Starts on the transponder the receiver is tuned to, reads the NIT and tunes every "
                               "transport it lists, collecting the services of each. The receiver must be running."));
    v->addWidget(intro);
    auto *controls = new QHBoxLayout;
    m_followNit = new QCheckBox(tr("Follow the NIT"));
    m_followNit->setChecked(true);
    m_nitOther = new QCheckBox(tr("Include other networks"));
    m_allPositions = new QCheckBox(tr("All orbital positions"));
    m_networkButton = new QPushButton(tr("Start network scan"));
    m_networkButton->setObjectName(QStringLiteral("Primary"));
    m_networkCancel = new QPushButton(tr("Cancel"));
    m_networkCancel->setObjectName(QStringLiteral("Ghost"));
    m_networkCancel->setEnabled(false);
    controls->addWidget(m_followNit);
    controls->addWidget(m_nitOther);
    controls->addWidget(m_allPositions);
    controls->addStretch();
    controls->addWidget(m_networkButton);
    controls->addWidget(m_networkCancel);
    v->addLayout(controls);

    auto *statusRow = new QHBoxLayout;
    m_netProgress = new QProgressBar;
    m_netProgress->setRange(0, 1000);
    m_netProgress->setTextVisible(false);
    m_netProgress->setFixedWidth(180);
    m_netProgress->setVisible(false);
    m_netStatus = new QLabel;
    m_netStatus->setObjectName(QStringLiteral("Muted"));
    statusRow->addWidget(m_netProgress);
    statusRow->addWidget(m_netStatus, 1);
    v->addLayout(statusRow);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({tr("Transponder / service"), tr("Pol"), tr("Symbol rate"), tr("TSID / SID"), tr("Details")});
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    v->addWidget(m_tree, 1);

    auto *actions = new QHBoxLayout;
    m_netTune = new QPushButton(tr("Tune"));
    m_netSave = new QPushButton(tr("Save transponders as presets"));
    m_netSave->setObjectName(QStringLiteral("Ghost"));
    m_netTune->setEnabled(false);
    m_netSave->setEnabled(false);
    actions->addWidget(m_netTune);
    actions->addWidget(m_netSave);
    actions->addStretch();
    v->addLayout(actions);

    connect(m_networkButton, &QPushButton::clicked, this, &ScanDialog::startNetworkScan);
    connect(m_networkCancel, &QPushButton::clicked, this, &ScanDialog::cancel);
    connect(m_followNit, &QCheckBox::toggled, this, [this](bool on) {
        m_nitOther->setEnabled(on);
        m_allPositions->setEnabled(on);
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() { m_netTune->setEnabled(!m_tree->selectedItems().isEmpty() && !m_busy); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this]() { tuneSelectedTransponder(); });
    connect(m_netTune, &QPushButton::clicked, this, &ScanDialog::tuneSelectedTransponder);
    connect(m_netSave, &QPushButton::clicked, this, &ScanDialog::saveTransponders);
    return w;
}

void ScanDialog::probeCapabilities()
{
    const ReceiverConfig cfg = m_hooks.config();
    if (cfg.device.kind != DeviceKind::LinuxDvb) {
        m_caps->setText(tr("Spectrum and blind scan need a Linux DVB tuner with the neumoDVB driver. "
                           "The network scan works with every receiver, also with file replay."));
        return;
    }
    if (m_hooks.running()) {
        m_caps->setText(tr("The receiver is running: it will be stopped for a spectrum measurement or blind scan."));
        return;
    }
    const ScanCapabilities c = Scan::capabilities(cfg.device);
    if (!c.probed)
        m_caps->setText(tr("Cannot query the tuner: %1").arg(QString::fromStdString(c.note)));
    else if (!c.neumoApi)
        m_caps->setText(QString::fromStdString(c.note));
    else
        m_caps->setText(tr("Driver support: spectrum %1, blind scan %2, constellation %3")
                            .arg(c.spectrumFft || c.spectrumSweep ? tr("yes") : tr("no"), c.blindScan ? tr("yes") : tr("no"),
                                 c.constellation ? tr("yes") : tr("no")));
}

void ScanDialog::setBusy(bool busy, const QString &what)
{
    m_busy = busy;
    for (QWidget *w : std::initializer_list<QWidget *>{m_spectrumButton, m_blindButton, m_networkButton, m_from, m_to, m_pol,
                                                        m_method, m_followNit})
        w->setEnabled(!busy);
    m_nitOther->setEnabled(!busy && m_followNit->isChecked());
    m_allPositions->setEnabled(!busy && m_followNit->isChecked());
    m_cancelButton->setEnabled(busy && what != QLatin1String("network"));
    m_networkCancel->setEnabled(busy && what == QLatin1String("network"));
    m_progress->setVisible(busy && what != QLatin1String("network"));
    m_netProgress->setVisible(busy && what == QLatin1String("network"));
    m_tuneButton->setEnabled(!busy && selectedCarrier() >= 0);
    m_netTune->setEnabled(!busy && !m_tree->selectedItems().isEmpty());
}

void ScanDialog::cancel()
{
    m_cancel = true;
    m_status->setText(tr("Cancelling…"));
    m_netStatus->setText(tr("Cancelling…"));
}

void ScanDialog::startFrontendScan(bool blind)
{
    if (m_busy)
        return;
    if (!(m_to->value() > m_from->value())) {
        m_status->setText(tr("The end frequency must be above the start frequency."));
        return;
    }
    if (m_hooks.running()) {
        const auto answer = QMessageBox::question(this, windowTitle(),
                                                  tr("The scan needs the tuner. Stop the receiver now?"),
                                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
            return;
        m_hooks.stopReceiver();
    }
    joinWorker();
    m_scanConfig = m_hooks.config();
    m_results.clear();
    m_chart->clear();
    m_carriers->setRowCount(0);
    m_showPol->setVisible(false);
    m_cancel = false;
    setBusy(true, blind ? QStringLiteral("blind") : QStringLiteral("spectrum"));
    m_progress->setValue(0);
    m_status->setText(blind ? tr("Blind scan…") : tr("Measuring…"));

    std::vector<Polarization> pols;
    const int p = m_pol->currentData().toInt();
    if (p == 0 || p == 2)
        pols.push_back(Polarization::HorizontalLeft18V);
    if (p == 1 || p == 2)
        pols.push_back(Polarization::VerticalRight13V);
    const double from = m_from->value(), to = m_to->value();
    const auto method = static_cast<SpectrumMethod>(m_method->currentData().toInt());
    const ReceiverConfig cfg = m_scanConfig;
    emit logMessage(blind ? tr("Blind scan %1-%2 MHz").arg(from).arg(to) : tr("Spectrum %1-%2 MHz").arg(from).arg(to));

    m_worker = std::thread([this, pols, from, to, method, cfg, blind]() {
        std::string error;
        for (size_t k = 0; k < pols.size(); ++k) {
            PolResult res;
            res.blind = blind;
            auto progress = [this, k, n = pols.size(), pol = pols[k]](double f, const std::string &msg) {
                if (f >= 0 || !msg.empty()) {
                    const double total = f >= 0 ? (k + f) / n : -1.0;
                    const QString text = msg.empty() ? QString()
                                                     : polLetter(pol) + QStringLiteral(": ") + QString::fromStdString(msg);
                    QMetaObject::invokeMethod(this, [this, total, text]() {
                        if (total >= 0)
                            m_progress->setValue(static_cast<int>(total * 1000));
                        if (!text.isEmpty())
                            m_status->setText(text);
                    }, Qt::QueuedConnection);
                }
                return !m_cancel.load();
            };
            bool ok;
            if (blind) {
                BlindScanRequest req;
                req.startMHz = from;
                req.endMHz = to;
                req.lnb = cfg.tuning;
                req.lnb.polarization = pols[k];
                ok = Scan::blindScan(cfg.device, req, &res.carriers, &error, progress, {}, &res.spectrum);
                res.spectrum.polarization = pols[k];
            } else {
                SpectrumRequest req;
                req.startMHz = from;
                req.endMHz = to;
                req.lnb = cfg.tuning;
                req.lnb.polarization = pols[k];
                req.method = method;
                ok = Scan::spectrum(cfg.device, req, &res.spectrum, &error, progress);
            }
            if (!ok)
                break;
            QMetaObject::invokeMethod(this, [this, res]() {
                m_results.push_back(res);
                const QString name = res.spectrum.polarization == Polarization::VerticalRight13V ? tr("Vertical") : tr("Horizontal");
                m_showPol->addItem(name);
                m_showPol->setVisible(m_showPol->count() > 1);
                if (m_results.size() == 1)
                    showPolarization(0);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, error, blind]() {
            joinWorker();
            setBusy(false);
            size_t carriers = 0, peaks = 0;
            for (const auto &r : m_results) {
                carriers += r.carriers.size();
                peaks += r.spectrum.peaks.size();
            }
            if (!error.empty())
                m_status->setText(QString::fromStdString(error));
            else if (blind)
                m_status->setText(tr("%n carrier(s) locked", nullptr, static_cast<int>(carriers)));
            else
                m_status->setText(tr("%n carrier candidate(s); run a blind scan to lock and identify them", nullptr,
                                     static_cast<int>(peaks)));
            emit logMessage(error.empty() ? m_status->text() : QStringLiteral("WARNING: scan: ") + m_status->text());
            m_saveAllButton->setEnabled(carriers + peaks > 0);
            m_csvButton->setEnabled(!m_results.empty());
        }, Qt::QueuedConnection);
    });
}

void ScanDialog::showPolarization(int index)
{
    if (index < 0 || index >= static_cast<int>(m_results.size()))
        return;
    m_shown = index;
    const PolResult &r = m_results[static_cast<size_t>(index)];
    QVector<SpectrumChart::Point> pts;
    pts.reserve(static_cast<int>(r.spectrum.points.size()));
    for (const auto &p : r.spectrum.points)
        pts.push_back({p.frequencyMHz, p.level});
    m_chart->setSpectrum(pts, r.spectrum.decibel);
    if (pts.isEmpty())
        m_chart->setPlaceholder(tr("The driver returned no spectrum data"));
    fillCarrierTable();
}

void ScanDialog::fillCarrierTable()
{
    m_carriers->setRowCount(0);
    QVector<SpectrumChart::Marker> markers;
    if (m_shown < 0)
        return;
    const PolResult &r = m_results[static_cast<size_t>(m_shown)];
    const QString pol = polLetter(r.spectrum.polarization);
    int row = 0;
    if (r.blind) {
        m_carriers->setRowCount(static_cast<int>(r.carriers.size()));
        for (const BlindScanCarrier &c : r.carriers) {
            const QString mod = c.modulation >= 0 ? QString::fromStdString(DvbNames::modulation(c.modulation)) : QString();
            const QString fec = c.fec >= 0 ? QString::fromStdString(DvbNames::fec(c.fec)) : QString();
            m_carriers->setItem(row, 0, cell(QStringLiteral("%1 MHz").arg(c.frequencyMHz, 0, 'f', 3), true));
            m_carriers->setItem(row, 1, cell(pol));
            m_carriers->setItem(row, 2, cell(QStringLiteral("%1 kS/s").arg(c.symbolRateKsps, 0, 'f', 0), true));
            m_carriers->setItem(row, 3, cell(c.deliverySystem == DeliverySystem::DvbS ? QStringLiteral("DVB-S") : QStringLiteral("DVB-S2")));
            m_carriers->setItem(row, 4, cell((mod + QLatin1Char(' ') + fec).trimmed()));
            m_carriers->setItem(row, 5, cell(c.hasCnrDb ? QStringLiteral("%1 dB").arg(c.cnrDb, 0, 'f', 1) : QString(), true));
            QString isis;
            for (int i : c.isis)
                isis += (isis.isEmpty() ? QString() : QStringLiteral(", ")) + QString::number(i);
            m_carriers->setItem(row, 6, cell(c.isis.size() > 1 ? isis : tr("single")));
            m_carriers->setItem(row, 7, cell(tr("locked")));
            markers.push_back({c.frequencyMHz, c.symbolRateKsps * 1.2 / 1000.0,
                               QStringLiteral("%1 kS/s · %2 %3").arg(c.symbolRateKsps, 0, 'f', 0)
                                   .arg(c.deliverySystem == DeliverySystem::DvbS ? QStringLiteral("DVB-S") : QStringLiteral("DVB-S2"), mod),
                               true});
            ++row;
        }
    } else {
        m_carriers->setRowCount(static_cast<int>(r.spectrum.peaks.size()));
        for (const SpectrumPeak &pk : r.spectrum.peaks) {
            m_carriers->setItem(row, 0, cell(QStringLiteral("%1 MHz").arg(pk.frequencyMHz, 0, 'f', 1), true));
            m_carriers->setItem(row, 1, cell(pol));
            m_carriers->setItem(row, 2, cell(QStringLiteral("~%1 kS/s").arg(pk.symbolRateKsps, 0, 'f', 0), true));
            for (int c = 3; c < 7; ++c)
                m_carriers->setItem(row, c, cell(QString()));
            m_carriers->setItem(row, 7, cell(tr("candidate")));
            markers.push_back({pk.frequencyMHz, pk.symbolRateKsps * 1.2 / 1000.0,
                               tr("candidate, ~%1 kS/s").arg(pk.symbolRateKsps, 0, 'f', 0), false});
            ++row;
        }
    }
    m_carriers->resizeColumnsToContents();
    m_chart->setMarkers(markers);
}

int ScanDialog::selectedCarrier() const
{
    const auto rows = m_carriers->selectionModel() ? m_carriers->selectionModel()->selectedRows() : QModelIndexList();
    return rows.isEmpty() ? -1 : rows.front().row();
}

TuningParams ScanDialog::carrierTuning(int row) const
{
    TuningParams t = m_scanConfig.tuning;
    if (m_shown < 0 || row < 0)
        return t;
    const PolResult &r = m_results[static_cast<size_t>(m_shown)];
    if (r.blind && row < static_cast<int>(r.carriers.size())) {
        PresetStore::applyCarrier(r.carriers[static_cast<size_t>(row)].tuning, t);
    } else if (!r.blind && row < static_cast<int>(r.spectrum.peaks.size())) {
        const SpectrumPeak &pk = r.spectrum.peaks[static_cast<size_t>(row)];
        t.frequencyMHz = std::round(pk.frequencyMHz * 10.0) / 10.0;
        t.symbolRateKsps = std::max(1000.0, std::round(pk.symbolRateKsps / 100.0) * 100.0);
        t.polarization = r.spectrum.polarization;
        t.modulation = ModulationHint::Auto;
        t.inputStreamId = -1;
    }
    return t;
}

void ScanDialog::saveCarriers(bool all)
{
    QList<PresetStore::Preset> list;
    if (all) {
        for (size_t k = 0; k < m_results.size(); ++k) {
            const int saved = m_shown;
            m_shown = static_cast<int>(k);
            const PolResult &r = m_results[k];
            const int n = static_cast<int>(r.blind ? r.carriers.size() : r.spectrum.peaks.size());
            for (int i = 0; i < n; ++i) {
                const TuningParams t = carrierTuning(i);
                list.push_back({PresetStore::defaultName(t), t});
            }
            m_shown = saved;
        }
    } else {
        const int row = selectedCarrier();
        if (row < 0)
            return;
        const TuningParams t = carrierTuning(row);
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Name"), QLineEdit::Normal,
                                                   PresetStore::defaultName(t), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        list.push_back({name.trimmed(), t});
    }
    PresetStore::instance().saveAll(list);
    m_status->setText(tr("%n preset(s) saved", nullptr, static_cast<int>(list.size())));
}

void ScanDialog::exportCsv()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Export spectrum"), QStringLiteral("spectrum.csv"),
                                                      tr("CSV files (*.csv)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_status->setText(tr("Cannot write %1").arg(file));
        return;
    }
    QTextStream out(&f);
    out << "frequency_mhz,polarization,level\n";
    for (const PolResult &r : m_results)
        for (const SpectrumPoint &p : r.spectrum.points)
            out << QString::number(p.frequencyMHz, 'f', 3) << ',' << polLetter(r.spectrum.polarization) << ','
                << QString::number(p.level, 'f', 2) << '\n';
    m_status->setText(tr("Spectrum written to %1").arg(file));
}

// ---------------------------------------------------------------------------
// Network scan
// ---------------------------------------------------------------------------

void ScanDialog::startNetworkScan()
{
    if (m_busy)
        return;
    if (!m_hooks.running() || !m_hooks.receiver() || !m_hooks.processor()) {
        m_netStatus->setText(tr("Start the receiver on a transponder of the network first."));
        return;
    }
    joinWorker();
    m_tree->clear();
    m_transponders.clear();
    m_netSave->setEnabled(false);
    m_cancel = false;
    setBusy(true, QStringLiteral("network"));
    m_netProgress->setValue(0);
    m_netStatus->setText(tr("Scanning…"));

    bb::NetworkScanOptions opt;
    opt.followNit = m_followNit->isChecked();
    opt.includeNitOther = m_nitOther->isChecked();
    opt.sameOrbitalPosition = !m_allPositions->isChecked();
    Receiver *rx = m_hooks.receiver();
    std::shared_ptr<bb::Processor> proc = m_hooks.processor();
    const TuningParams seed = m_hooks.config().tuning;
    emit logMessage(tr("Network scan from %1").arg(QString::fromStdString(seed.describe())));

    m_worker = std::thread([this, rx, proc, seed, opt]() {
        const bb::NetworkScanResult r = bb::runNetworkScan(
            *rx, *proc, {seed}, opt,
            [this](const bb::NetworkScanProgress &p) {
                const QString msg = QString::fromStdString(p.message);
                const double f = p.total > 0 ? std::min(1.0, double(p.index) / p.total) : 0.0;
                const int idx = p.index, total = p.total;
                QMetaObject::invokeMethod(this, [this, msg, f, idx, total]() {
                    m_netProgress->setValue(static_cast<int>(f * 1000));
                    m_netStatus->setText(idx < total ? tr("[%1/%2] %3").arg(idx + 1).arg(total).arg(msg) : msg);
                }, Qt::QueuedConnection);
            },
            [this](const bb::ScannedTransponder &t) {
                QMetaObject::invokeMethod(this, [this, t]() { addTransponder(t); }, Qt::QueuedConnection);
            },
            &m_cancel);
        // Back to where the scan started
        if (rx->isRunning())
            rx->retune(seed);
        QMetaObject::invokeMethod(this, [this, r]() {
            joinWorker();
            setBusy(false);
            QString text = tr("%n transponder(s)", nullptr, static_cast<int>(r.transponders.size())) + QStringLiteral(", ")
                + tr("%n service(s)", nullptr, static_cast<int>(r.serviceCount()));
            if (!r.networkName.empty())
                text = QStringLiteral("%1 — ").arg(QString::fromStdString(r.networkName)) + text;
            if (r.cancelled)
                text += tr(" (cancelled)");
            m_netStatus->setText(text);
            emit logMessage(tr("Network scan: %1").arg(text));
            m_netSave->setEnabled(!m_transponders.empty());
        }, Qt::QueuedConnection);
    });
}

void ScanDialog::addTransponder(const bb::ScannedTransponder &t)
{
    m_transponders.push_back(t);
    auto *item = new QTreeWidgetItem(m_tree);
    item->setData(0, Qt::UserRole, static_cast<int>(m_transponders.size() - 1));
    QString name = QStringLiteral("%1 MHz").arg(t.tuning.frequencyMHz, 0, 'f', 3);
    if (t.tuning.inputStreamId >= 0)
        name += tr(" · ISI %1").arg(t.tuning.inputStreamId);
    item->setText(0, name);
    item->setText(1, polLetter(t.tuning.polarization));
    item->setText(2, QStringLiteral("%1 kS/s").arg(t.tuning.symbolRateKsps, 0, 'f', 0));
    if (!t.locked) {
        item->setText(4, tr("no lock"));
        item->setForeground(4, Theme::tokens().critical);
        return;
    }
    item->setText(3, QString::number(t.tsid));
    QString details = tr("%n service(s)", nullptr, static_cast<int>(t.services.size()));
    if (!t.networkName.empty())
        details += QStringLiteral(" · ") + QString::fromStdString(t.networkName);
    if (!t.tablesComplete)
        details += tr(" · tables incomplete");
    item->setText(4, details);
    for (const bb::ServiceInfo &s : t.services) {
        auto *child = new QTreeWidgetItem(item);
        child->setData(0, Qt::UserRole, item->data(0, Qt::UserRole));
        child->setText(0, QString::fromStdString(s.name.empty() ? std::string("(no name)") : s.name));
        child->setText(3, QString::number(s.serviceId));
        QString d = QString::fromStdString(s.typeName);
        if (!s.provider.empty())
            d += QStringLiteral(" · ") + QString::fromStdString(s.provider);
        if (s.scrambled)
            d += tr(" · scrambled");
        child->setText(4, d);
    }
    item->setExpanded(m_transponders.size() <= 3);
}

void ScanDialog::tuneSelectedTransponder()
{
    const auto items = m_tree->selectedItems();
    if (items.isEmpty())
        return;
    const int idx = items.front()->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < static_cast<int>(m_transponders.size()))
        emit tuneRequested(m_transponders[static_cast<size_t>(idx)].tuning);
}

void ScanDialog::saveTransponders()
{
    QList<PresetStore::Preset> list;
    for (const auto &t : m_transponders) {
        if (!t.locked)
            continue;
        QString name = PresetStore::defaultName(t.tuning);
        if (!t.services.empty())
            name += QStringLiteral(" — ") + QString::fromStdString(t.services.front().name);
        list.push_back({name, t.tuning});
    }
    PresetStore::instance().saveAll(list);
    m_netStatus->setText(tr("%n preset(s) saved", nullptr, static_cast<int>(list.size())));
}
