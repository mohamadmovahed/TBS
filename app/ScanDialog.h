#pragma once

#include <tbsbbproc/NetworkScan.h>
#include <tbsdvb/Receiver.h>
#include <tbsdvb/Scan.h>

#include <QDialog>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTabWidget;
class SpectrumChart;

// Spectrum, blind scan and NIT network scan for one receiver.
//   Spectrum / blind scan need the tuner to themselves (the receiver is stopped first) and the
//   neumoDVB driver; the network scan retunes the running receiver and reads its SI tables.
class ScanDialog : public QDialog
{
    Q_OBJECT

public:
    struct Hooks
    {
        std::function<tbs::ReceiverConfig()> config;                   // device + LNB settings
        std::function<bool()> running;                                 // receiver running
        std::function<void()> stopReceiver;
        std::function<tbs::Receiver *()> receiver;                     // while running
        std::function<std::shared_ptr<tbs::bb::Processor>()> processor; // while running
    };

    ScanDialog(Hooks hooks, const QString &receiverName, QWidget *parent = nullptr);
    ~ScanDialog() override;

signals:
    // Tune the receiver to this carrier (LNB settings are the receiver's); start if stopped
    void tuneRequested(const tbs::TuningParams &carrier);
    void logMessage(const QString &message);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QWidget *buildSpectrumTab();
    QWidget *buildNetworkTab();
    void probeCapabilities();
    void startFrontendScan(bool blind);
    void startNetworkScan();
    void cancel();
    void setBusy(bool busy, const QString &what = QString());
    void joinWorker();
    void showPolarization(int index);
    void fillCarrierTable();
    int selectedCarrier() const;
    tbs::TuningParams carrierTuning(int row) const;
    void saveCarriers(bool all);
    void exportCsv();
    void addTransponder(const tbs::bb::ScannedTransponder &t);
    void tuneSelectedTransponder();
    void saveTransponders();

    Hooks m_hooks;
    std::thread m_worker;
    std::atomic<bool> m_cancel{false};
    bool m_busy = false;

    QLabel *m_caps = nullptr;
    QTabWidget *m_tabs = nullptr;

    // Spectrum tab
    QDoubleSpinBox *m_from = nullptr;
    QDoubleSpinBox *m_to = nullptr;
    QComboBox *m_pol = nullptr;
    QComboBox *m_method = nullptr;
    QPushButton *m_spectrumButton = nullptr;
    QPushButton *m_blindButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QComboBox *m_showPol = nullptr;
    SpectrumChart *m_chart = nullptr;
    QTableWidget *m_carriers = nullptr;
    QPushButton *m_tuneButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_saveAllButton = nullptr;
    QPushButton *m_csvButton = nullptr;

    struct PolResult
    {
        tbs::SpectrumResult spectrum;
        std::vector<tbs::BlindScanCarrier> carriers; // blind scan
        bool blind = false;
    };
    std::vector<PolResult> m_results;
    int m_shown = -1;
    tbs::ReceiverConfig m_scanConfig; // LNB settings used for the results

    // Network tab
    QCheckBox *m_followNit = nullptr;
    QCheckBox *m_nitOther = nullptr;
    QCheckBox *m_allPositions = nullptr;
    QPushButton *m_networkButton = nullptr;
    QPushButton *m_networkCancel = nullptr;
    QProgressBar *m_netProgress = nullptr;
    QLabel *m_netStatus = nullptr;
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_netTune = nullptr;
    QPushButton *m_netSave = nullptr;
    std::vector<tbs::bb::ScannedTransponder> m_transponders;
};
