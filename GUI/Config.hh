#pragma once
#include <QtCore>
#include <array>
#include <stdexcept>
#include "r5560_registers.h"

namespace daq {
constexpr int Channels = 32;
constexpr int ParameterCount = 9;
constexpr quint32 ApiVersion = 0x00020000;
constexpr quint32 DmaApiVersion = 0x00030000;
constexpr quint32 DmaInterfaceVersion = 0x00010000;
inline bool supportedApi(quint32 api) { return api == ApiVersion || api == DmaApiVersion; }
constexpr quint32 ClockHz = 125000000;
constexpr quint32 Armed = 1, Running = 2, Draining = 4, Idle = 8, ClockGood = 16;
constexpr quint32 FifoReady = 512, Preparing = 1024;
struct Error : std::runtime_error {
    explicit Error(const QString &s) : std::runtime_error(s.toStdString()) {}
};
// Preserve the editor location independently of the human-readable message.
struct SettingsError : Error {
    int module, channel, parameter;
    explicit SettingsError(const QString &text, int mod = -1, int ch = -1, int par = -1)
        : Error(text), module(mod), channel(ch), parameter(par) {}
};
struct Parameter {
    const char *name;
    const char *title;
    quint32 address, minimum, maximum, initial;
    const char *help;
};
extern const std::array<Parameter, ParameterCount> Parameters;
using ChannelSettings = std::array<quint32, ParameterCount>;
struct ModuleSettings {
    std::array<ChannelSettings, Channels> channels;
    // R5560 baseboard sync_reg.1 reaches physical SYNC_1; independent T0 is opt-in.
    quint32 syncConfig = 0x03, channelMask = 0xffffffff;
    ModuleSettings();
    void validate() const;
    QJsonObject json() const;
    static ModuleSettings fromJson(const QJsonObject &o);
};
struct Endpoint {
    QString ip;
    quint16 port = 8888;
    quint32 id = 0;
};
struct Config {
    QVector<Endpoint> endpoints;
    QString filename, parameterFile, userFile;
    QStringList warnings;
    static Config load(const QString &path);
    QJsonObject json() const;
};
QVector<ModuleSettings> loadSettings(const QString &path, int modules);
void validateSettings(const QVector<ModuleSettings> &settings);
void validateModuleSettings(const ModuleSettings &settings, int module);
void saveSettings(const QString &path, const QVector<ModuleSettings> &settings);
void applyUserFile(const QString &path, QVector<ModuleSettings> &settings);
quint32 parseUInt(const QString &text, const QString &field);
quint32 jsonUInt(const QJsonValue &value, const QString &field);
QJsonObject readObject(const QString &path);
void writeObject(const QString &path, const QJsonObject &object);
QString statusText(quint32 status);
} // namespace daq
