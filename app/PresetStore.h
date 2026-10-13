#pragma once

#include <tbsdvb/Types.h>

#include <QList>
#include <QObject>
#include <QString>

// Named carriers ("presets"), shared by all receivers. A preset keeps the carrier part of a
// tuning (frequency, symbol rate, standard, modulation, polarization, DiSEqC port, input
// stream, PLS); the LNB settings stay those of the receiver it is applied to.
class PresetStore : public QObject
{
    Q_OBJECT

public:
    struct Preset
    {
        QString name;
        tbs::TuningParams tuning;
    };

    static PresetStore &instance();

    QList<Preset> presets() const;
    // Adds or replaces (same name)
    void save(const QString &name, const tbs::TuningParams &tuning);
    void saveAll(const QList<Preset> &presets); // adds/replaces several at once
    void remove(const QString &name);

    // Copies the carrier fields of a preset into a full tuning
    static void applyCarrier(const tbs::TuningParams &from, tbs::TuningParams &to);
    // "11494.000 H 27500" (+ " ISI 3")
    static QString defaultName(const tbs::TuningParams &t);

    // Text form used in the settings (also handy for tests)
    static QString encode(const Preset &p);
    static bool decode(const QString &s, Preset *p);

signals:
    void changed();

private:
    PresetStore() = default;
    void write(const QList<Preset> &list);
};
