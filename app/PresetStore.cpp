#include "PresetStore.h"

#include <QSettings>
#include <algorithm>
#include <cmath>
#include <QStringList>

namespace {
const QString kKey = QStringLiteral("presets/list");
}

PresetStore &PresetStore::instance()
{
    static PresetStore store;
    return store;
}

QString PresetStore::encode(const Preset &p)
{
    const tbs::TuningParams &t = p.tuning;
    QString name = p.name;
    name.replace(QLatin1Char('|'), QLatin1Char('/'));
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
        .arg(name)
        .arg(t.frequencyMHz, 0, 'f', 3)
        .arg(t.symbolRateKsps, 0, 'f', 3)
        .arg(static_cast<int>(t.deliverySystem))
        .arg(static_cast<int>(t.modulation))
        .arg(static_cast<int>(t.polarization))
        .arg(t.diseqcPort)
        .arg(t.inputStreamId)
        .arg(static_cast<int>(t.plsMode))
        .arg(t.plsCode);
}

bool PresetStore::decode(const QString &s, Preset *p)
{
    const QStringList f = s.split(QLatin1Char('|'));
    if (f.size() < 10 || f[0].trimmed().isEmpty())
        return false;
    bool ok1 = false, ok2 = false;
    p->name = f[0];
    tbs::TuningParams &t = p->tuning;
    t.frequencyMHz = f[1].toDouble(&ok1);
    t.symbolRateKsps = f[2].toDouble(&ok2);
    if (!ok1 || !ok2 || t.frequencyMHz <= 0)
        return false;
    t.deliverySystem = f[3].toInt() == 0 ? tbs::DeliverySystem::DvbS : tbs::DeliverySystem::DvbS2;
    t.modulation = static_cast<tbs::ModulationHint>(qBound(0, f[4].toInt(), 4));
    t.polarization = static_cast<tbs::Polarization>(qBound(0, f[5].toInt(), 2));
    t.diseqcPort = qBound(-1, f[6].toInt(), 3);
    t.inputStreamId = qBound(-1, f[7].toInt(), 255);
    t.plsMode = static_cast<tbs::PlsMode>(qBound(0, f[8].toInt(), 2));
    t.plsCode = f[9].toUInt() & 0x3FFFF;
    return true;
}

QList<PresetStore::Preset> PresetStore::presets() const
{
    QSettings s;
    QList<Preset> out;
    for (const QString &line : s.value(kKey).toStringList()) {
        Preset p;
        if (decode(line, &p))
            out.push_back(p);
    }
    return out;
}

void PresetStore::write(const QList<Preset> &list)
{
    QStringList lines;
    for (const Preset &p : list)
        lines << encode(p);
    QSettings s;
    s.setValue(kKey, lines);
    emit changed();
}

void PresetStore::save(const QString &name, const tbs::TuningParams &tuning)
{
    saveAll({Preset{name, tuning}});
}

void PresetStore::saveAll(const QList<Preset> &add)
{
    QList<Preset> list = presets();
    for (const Preset &p : add) {
        bool replaced = false;
        for (Preset &x : list)
            if (x.name == p.name) {
                x.tuning = p.tuning;
                replaced = true;
            }
        if (!replaced)
            list.push_back(p);
    }
    write(list);
}

void PresetStore::remove(const QString &name)
{
    QList<Preset> list = presets();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Preset &p) { return p.name == name; }), list.end());
    write(list);
}

void PresetStore::applyCarrier(const tbs::TuningParams &from, tbs::TuningParams &to)
{
    to.frequencyMHz = from.frequencyMHz;
    to.symbolRateKsps = from.symbolRateKsps;
    to.deliverySystem = from.deliverySystem;
    to.modulation = from.modulation;
    to.polarization = from.polarization;
    to.diseqcPort = from.diseqcPort;
    to.inputStreamId = from.inputStreamId;
    to.plsMode = from.plsMode;
    to.plsCode = from.plsCode;
}

QString PresetStore::defaultName(const tbs::TuningParams &t)
{
    QString s = QStringLiteral("%1 %2 %3")
                    .arg(t.frequencyMHz, 0, 'f', t.frequencyMHz == std::floor(t.frequencyMHz) ? 0 : 3)
                    .arg(t.polarization == tbs::Polarization::VerticalRight13V ? QStringLiteral("V") : QStringLiteral("H"))
                    .arg(qRound(t.symbolRateKsps));
    if (t.inputStreamId >= 0)
        s += QStringLiteral(" ISI %1").arg(t.inputStreamId);
    return s;
}
