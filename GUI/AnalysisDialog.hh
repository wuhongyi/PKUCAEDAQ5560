#pragma once
#include <QtWidgets>
#include <QProcess>
#include <QTemporaryDir>

class AnalysisDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit AnalysisDialog(QWidget *parent = nullptr);
    ~AnalysisDialog() override;

  private:
    QLineEdit *fileA, *fileB, *output;
    QSpinBox *channelA, *channelB, *windowNs, *offsetNs;
    QPlainTextEdit *log;
    QPushButton *decode, *coincidence, *cancel;
    QProcess process;
    QTemporaryDir scripts;
    void launch(bool pairs);
};
