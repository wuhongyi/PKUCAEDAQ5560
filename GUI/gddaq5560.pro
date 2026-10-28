QT += core gui widgets network
CONFIG += c++17
TARGET = gddaq5560
TEMPLATE = app
SOURCES += Debug.cc Waveform.cc main.cc MainWindow.cc SettingsWindows.cc AnalysisDialog.cc ControlPanel.cc OutputSettingDialog.cc \
    DmaReceiver.cc Config.cc EventDecoder.cc DeviceHandle.cc ReadoutThread.cc Acquisition.cc
HEADERS += Debug.hh Waveform.hh MainWindow.hh SettingsWindows.hh AnalysisDialog.hh ControlPanel.hh OutputSettingDialog.hh \
    DmaReceiver.hh Config.hh EventDecoder.hh DeviceHandle.hh ReadoutThread.hh Acquisition.hh r5560_registers.h
RESOURCES += gddaq.qrc
win32:CONFIG += console
unix:QMAKE_CXXFLAGS += -pthread
unix:QMAKE_LFLAGS += -pthread
unix:target.path = /usr/local/bin
unix:INSTALLS += target
