#pragma once
#include <QtWidgets>
#include "Acquisition.hh"

class WaveformPlot final : public QWidget {
    Q_OBJECT
  public:
    explicit WaveformPlot(QWidget *parent = nullptr);
    void setWaveform(const daq::Waveform &waveform);
    void setBaseline(int first, int count);
    void autoScale();
  signals:
    void cursorText(const QString &text);

  protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

  private:
    daq::Waveform wave;
    double xLow = 0, xHigh = 1, yLow = 0, yHigh = 16383;
    int baselineFirst = 0, baselineCount = 128;
    bool dragging = false, zoomed = false;
    QPointF dragStart, dragEnd;
    QRectF plotRect() const;
    QPointF toData(const QPointF &point) const;
};

class Debug final : public QMainWindow {
    Q_OBJECT
  public:
    explicit Debug(daq::Acquisition *acq, QWidget *parent = nullptr);
    QPlainTextEdit *logWidget() const {
        return logView;
    }

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    daq::Acquisition *acq;
    QToolBox *modules;
    QComboBox *channel;
    QSpinBox *length, *pretrigger, *timeout, *baselineFirst, *baselineCount;
    QCheckBox *continuous;
    QPushButton *sent, *force, *stop, *save;
    QLabel *info, *statistics;
    WaveformPlot *plot;
    QPlainTextEdit *logView;
    QTimer nextCapture;
    daq::Waveform wave;
    bool capturing = false, repeatForce = false;
    void reloadModules();
    void updateControls();
    void capture(bool forceTrigger);
    void cancel();
    void updateStatistics();
};
