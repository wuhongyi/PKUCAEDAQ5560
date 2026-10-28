#include "Config.hh"
#include <QHostAddress>
#include <cmath>

namespace daq {
const std::array<Parameter, ParameterCount> Parameters{
    {{"POL", "Polarity", R5560_POL, 0, 1, 1, "1 = positive; 0 = negative"},
     {"THRE", "Threshold", R5560_THRE, 0, 16383, 80, "14-bit ADC threshold"},
     {"TRIGFL", "FL", R5560_TRIGFL, 1, 127, 8, "Trigger filter length, samples (8 ns/sample)"},
     {"TRIGFLFG", "FL+FG", R5560_TRIGFLFG, 1, 127, 16, "Must be >= FL, samples"},
     {"BLLEN", "BL", R5560_BLLEN, 1, 1023, 128, "Baseline length, samples"},
     {"XIAOFFSET", "XIA offset", R5560_XIAOFFSET, 0, 255, 16, "Samples"},
     {"XIASL", "XIA SL", R5560_XIASL, 1, 1023, 625, "625 samples = 5 us"},
     {"XIASG", "XIA SG", R5560_XIASG, 1, 1023, 125,
      "125 samples = 1 us; zero is unsafe in current RTL"},
     {"TAU", "Tau (ns)", R5560_TAU, 1, 0xffffffffu, 50000,
      "Decay time in ns for online energy reconstruction; RTL stores this value only"}}};
quint32 parseUInt(const QString &text, const QString &field) {
    QString t = text.trimmed();
    bool ok = false;
    if (t.startsWith('-') || t.isEmpty())
        throw Error(field + ": unsigned integer required");
    quint64 v = t.toULongLong(&ok, t.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
    if (!ok || v > 0xffffffffu)
        throw Error(field + ": expected 0..4294967295");
    return quint32(v);
}
quint32 jsonUInt(const QJsonValue &v, const QString &field) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < 0 ||
        v.toDouble() > 4294967295.0 || std::floor(v.toDouble()) != v.toDouble())
        throw Error(field + ": unsigned JSON integer required");
    return quint32(v.toDouble());
}
ModuleSettings::ModuleSettings() {
    for (auto &ch : channels)
        for (int p = 0; p < ParameterCount; ++p)
            ch[p] = Parameters[p].initial;
}
void ModuleSettings::validate() const {
    if (syncConfig & ~0x7fu)
        throw Error("SYNC_CONFIG: reserved bits must be zero");
    if (((syncConfig >> 1) & 3) > 2 || ((syncConfig >> 3) & 3) > 2)
        throw Error("SYNC selectors must be 0, 1 or 2");
    if ((syncConfig & 0x21) == 0x21 && ((syncConfig >> 1) & 3) == ((syncConfig >> 3) & 3))
        throw Error("RUN and T0 must use different SYNC inputs");
    if (!channelMask)
        throw Error("At least one channel must be enabled");
    for (int ch = 0; ch < Channels; ++ch) {
        for (int p = 0; p < ParameterCount; ++p)
            if (channels[ch][p] < Parameters[p].minimum || channels[ch][p] > Parameters[p].maximum)
                throw SettingsError(QString("Ch %1 %2: got %5; expected %3..%4")
                                .arg(ch)
                                .arg(Parameters[p].name)
                                .arg(Parameters[p].minimum)
                                .arg(Parameters[p].maximum)
                                .arg(channels[ch][p]), -1, ch, p);
        if (channels[ch][3] < channels[ch][2])
            throw SettingsError(QString("Ch %1: FL+FG (%2) must be >= FL (%3)")
                            .arg(ch).arg(channels[ch][3]).arg(channels[ch][2]), -1, ch, 3);
    }
}
void validateSettings(const QVector<ModuleSettings> &settings) {
    for (int mod = 0; mod < settings.size(); ++mod)
        validateModuleSettings(settings[mod], mod);
}
void validateModuleSettings(const ModuleSettings &settings, int mod) {
    try {
        settings.validate();
    } catch (const SettingsError &e) {
        throw SettingsError(QString("Mod %1: %2").arg(mod).arg(QString::fromUtf8(e.what())),
                            mod, e.channel, e.parameter);
    } catch (const Error &e) {
        throw SettingsError(QString("Mod %1: %2").arg(mod).arg(QString::fromUtf8(e.what())), mod);
    }
}
QJsonObject ModuleSettings::json() const {
    QJsonArray rows;
    for (const auto &ch : channels) {
        QJsonObject row;
        for (int p = 0; p < ParameterCount; ++p)
            row[Parameters[p].name] = double(ch[p]);
        rows.append(row);
    }
    return {{"sync_config", double(syncConfig)},
            {"channel_enable", double(channelMask)},
            {"channels", rows}};
}
ModuleSettings ModuleSettings::fromJson(const QJsonObject &o) {
    ModuleSettings s;
    s.syncConfig = jsonUInt(o["sync_config"], "sync_config");
    s.channelMask = jsonUInt(o["channel_enable"], "channel_enable");
    auto rows = o["channels"].toArray();
    if (rows.size() != Channels)
        throw Error("Each module must contain exactly 32 channels");
    for (int ch = 0; ch < Channels; ++ch)
        for (int p = 0; p < ParameterCount; ++p)
            s.channels[ch][p] =
                jsonUInt(rows[ch].toObject()[Parameters[p].name], Parameters[p].name);
    s.validate();
    return s;
}
QJsonObject readObject(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw Error(path + ": " + f.errorString());
    if (f.size() > 16 * 1024 * 1024)
        throw Error("Configuration exceeds 16 MiB");
    QJsonParseError e;
    auto doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject())
        throw Error(path + ": invalid JSON object: " + e.errorString());
    return doc.object();
}
void writeObject(const QString &path, const QJsonObject &o) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        throw Error(f.errorString());
    auto b = QJsonDocument(o).toJson();
    if (f.write(b) != b.size() || !f.commit())
        throw Error("Cannot save " + path + ": " + f.errorString());
}
Config Config::load(const QString &path) {
    const auto o = readObject(path);
    Config c;
    c.filename = QFileInfo(path).absoluteFilePath();
    auto count = jsonUInt(o["modules"], "modules");
    const auto ips = o["ip"].toArray();
    if (count < 1 || count > 64 || ips.size() != int(count))
        throw Error("modules must be 1..64 and match the ip array");
    const auto ids = o["daq_ids"].toArray(), ports = o["ports"].toArray();
    if ((o.contains("daq_ids") && ids.size() != int(count)) ||
        (o.contains("ports") && ports.size() != int(count)))
        throw Error("daq_ids/ports length must match modules");
    QSet<QString> addresses;
    QSet<quint32> usedIds;
    for (int i = 0; i < int(count); ++i) {
        Endpoint ep;
        ep.ip = ips[i].toString();
        QHostAddress host(ep.ip);
        if (host.protocol() != QAbstractSocket::IPv4Protocol || host.isNull() ||
            host == QHostAddress::AnyIPv4)
            throw Error("Invalid IPv4 address: " + ep.ip);
        const quint32 port = ports.isEmpty() ? 8888 : jsonUInt(ports[i], "port");
        if (port < 1 || port > 65535)
            throw Error("port must be 1..65535");
        ep.port = quint16(port);
        ep.id = ids.isEmpty() ? quint32(i) : jsonUInt(ids[i], "DAQ ID");
        QString key = host.toString() + ":" + QString::number(ep.port);
        if (addresses.contains(key) || usedIds.contains(ep.id))
            throw Error("Duplicate endpoint or DAQ ID");
        addresses.insert(key);
        usedIds.insert(ep.id);
        c.endpoints.append(ep);
    }
    const QDir base = QFileInfo(c.filename).absoluteDir();
    auto resolve = [&](const char *key) {
        const auto v = o[key];
        if (v.isUndefined())
            return QString();
        if (!v.isString())
            throw Error(QString(key) + " must be a path string");
        if (v.toString().isEmpty())
            return QString();
        return QDir::cleanPath(base.absoluteFilePath(v.toString()));
    };
    c.parameterFile = resolve("par");
    c.userFile = resolve("userpars");
    return c;
}
QJsonObject Config::json() const {
    QJsonArray ips, ids, ports;
    for (const auto &ep : endpoints) {
        ips.append(ep.ip);
        ids.append(double(ep.id));
        ports.append(ep.port);
    }
    return {
        {"modules", endpoints.size()}, {"ip", ips},           {"daq_ids", ids}, {"ports", ports},
        {"par", parameterFile},        {"userpars", userFile}};
}
QVector<ModuleSettings> loadSettings(const QString &path, int modules) {
    auto o = readObject(path);
    if (o["schema"].toString() != "gddaq-r5560-settings-v1")
        throw Error("Not an R5560 settings file; CAEN FELib /par paths cannot be applied to Avalon "
                    "registers");
    auto a = o["modules"].toArray();
    if (a.size() != modules)
        throw Error("Settings module count differs from the connected crate");
    QVector<ModuleSettings> s;
    for (auto v : a)
        s.append(ModuleSettings::fromJson(v.toObject()));
    return s;
}
void saveSettings(const QString &path, const QVector<ModuleSettings> &s) {
    validateSettings(s);
    QJsonArray a;
    for (auto &m : s) {
        a.append(m.json());
    }
    writeObject(path, {{"schema", "gddaq-r5560-settings-v1"}, {"modules", a}});
}
void applyUserFile(const QString &path, QVector<ModuleSettings> &settings) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw Error(f.errorString());
    auto copy = settings;
    QTextStream in(&f);
    int line = 0;
    const QRegularExpression re("^(\\d+)\\s+/ch/(\\d+)(?:\\.\\.(\\d+))?/par/([A-Z]+)\\s+(\\S+)$");
    while (!in.atEnd()) {
        auto s = in.readLine().section('#', 0, 0).trimmed();
        ++line;
        if (s.isEmpty())
            continue;
        auto m = re.match(s);
        if (!m.hasMatch())
            throw Error(QString("%1:%2: unsupported command. Expected: module /ch/0..31/par/THRE "
                                "80. FELib sync commands are not R5560 commands.")
                            .arg(path)
                            .arg(line));
        int mod = m.captured(1).toInt(), first = m.captured(2).toInt(),
            last = m.captured(3).isEmpty() ? first : m.captured(3).toInt(), p = -1;
        for (int i = 0; i < ParameterCount; ++i)
            if (m.captured(4) == Parameters[i].name)
                p = i;
        if (mod < 0 || mod >= copy.size() || first < 0 || first > last || last >= Channels || p < 0)
            throw Error(QString("Invalid module/channel/parameter at line %1").arg(line));
        auto v = parseUInt(m.captured(5), m.captured(4));
        for (int ch = first; ch <= last; ++ch)
            copy[mod].channels[ch][p] = v;
    }
    for (auto &s : copy)
        s.validate();
    settings = copy;
}
QString statusText(quint32 s) {
    if (s & Preparing)
        return "Preparing";
    if (s & Running)
        return "Running";
    if (s & Draining)
        return "Draining";
    if (s & Armed)
        return "Armed / wait RUN";
    if (s & Idle)
        return "Idle";
    return "Not ready";
}
} // namespace daq
