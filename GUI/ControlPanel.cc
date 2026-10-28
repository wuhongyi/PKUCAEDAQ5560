// ControlPanel.cc --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 4月 23 21:07:44 2022 (+0800)
// Last-Updated: 六 7月 11 13:26:55 2026 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 38
// URL: http://wuhongyi.cn 

#include "ControlPanel.hh"
#include "MainWindow.hh"

#include <QHostAddress>
#include <QNetworkInterface>



#include <cstring>
#include <iostream>
#include <cstdio>
#include <iomanip>
#include <cstdlib>
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

ControlPanel::ControlPanel(MainWindow *parent)
: QWidget(parent), mMainWindow(parent)
{
  if (this->objectName().isEmpty())
    this->setObjectName(QString::fromUtf8("ControlPanel"));
  this->resize(200, 99);

  
  QPixmap logo;
  logo.load(":/images/control-logo.png");

  

  QLabel *systemip = new QLabel(this);
  char ip[1024];
  if(GetLocalIP(ip) == true)
    systemip->setText(ip);
  else
    systemip->setText("NO IP");
  systemip->setAlignment(Qt::AlignCenter);


  QLabel *labellogo = new QLabel(this);
  labellogo->setPixmap(logo);
  labellogo->setAlignment(Qt::AlignCenter);
  
  
  QVBoxLayout *verticallayoutip = new QVBoxLayout();
  verticallayoutip->setObjectName(QString::fromUtf8("verticallayoutip"));
  verticallayoutip->setContentsMargins(10, 10, 10, 10);
  verticallayoutip->addWidget(labellogo);
  verticallayoutip->setSpacing(40);
  verticallayoutip->addWidget(systemip);

  
  
  pbStartAcq = new QPushButton(this);
  pbStartAcq->setObjectName(QString::fromUtf8("pbStartAcq"));
  pbStartAcq->setMinimumSize(QSize(60, 22));
  pbStartAcq->setMaximumSize(QSize(100, 16777215));

  pbStopAcq = new QPushButton(this);
  pbStopAcq->setObjectName(QString::fromUtf8("pbStopAcq"));
  pbStopAcq->setMinimumSize(QSize(60, 22));
  pbStopAcq->setMaximumSize(QSize(100, 16777215));

  
  verticallayout = new QVBoxLayout();
  verticallayout->setObjectName(QString::fromUtf8("verticallayout"));
  verticallayout->addWidget(pbStartAcq);
  verticallayout->addWidget(pbStopAcq);


  gridlayout = new QGridLayout(this);
  gridlayout->setObjectName(QString::fromUtf8("gridlayout"));
  gridlayout->setContentsMargins(-1, 0, -1, 0);
  gridlayout->addLayout(verticallayoutip, 0, 0, 1, 3);
  gridlayout->addLayout(verticallayout, 3, 1, 2, 1);
  
  this->setWindowTitle(QApplication::translate("ControlPanel", "Form", nullptr));
  pbStartAcq->setText(QApplication::translate("ControlPanel", "Start", nullptr));
  pbStopAcq->setText(QApplication::translate("ControlPanel", "Stop", nullptr));
  
  trigger_timer = new QTimer(this);
  // QObject::connect(trigger_timer, SIGNAL(timeout()), mMainWindow, SLOT(SWTrg()));

  connect(pbStartAcq, SIGNAL(clicked()), mMainWindow, SLOT(StartAcquisition()));
  connect(pbStopAcq, SIGNAL(clicked()), mMainWindow, SLOT(StopAcquisition()));

  QMetaObject::connectSlotsByName(this);
}

ControlPanel::~ControlPanel()
{

}

void ControlPanel::DisableAll(bool f)
{
  pbStartAcq->setEnabled(!f);
  pbStopAcq->setEnabled(!f);
  // ui->pB_Trigger->setEnabled(!f);
}


void ControlPanel::ToogleStartStop()
{
  if(pbStartAcq->isEnabled())
    {
      pbStartAcq->setEnabled(false);
      pbStopAcq->setEnabled(true);
    }
  else
    {
      pbStartAcq->setEnabled(true);
      pbStopAcq->setEnabled(false);
    }
  
  // if (ui->pbStartAcq->isEnabled()) { //start acq 
    // 	ui->pbStartAcq->setEnabled(false);
    // 	ui->pbStopAcq->setEnabled(true);
    // 	if (ui->cBox_trigger_mode->currentIndex() == TRG_MODE_NORMAL)
    // 		ui->pB_Trigger->setEnabled(true);			
		
  // 	if(!mWaveDump->NAutoTrg)
  // 		trigger_timer->stop();
  // 	else {
  // 		if (!trigger_timer->isActive())
  // 			trigger_timer->start(500);
  // 	}
  // 	ui->dBox_reclen->setEnabled(false);
  // 	ui->dBox_pretrg->setEnabled(false);
  // 	ui->pB_trg_mask->setEnabled(false);
  // }
  // else { //stop acq 
    // 	ui->pbStartAcq->setEnabled(true);
    // 	ui->pbStopAcq->setEnabled(false);
    // 	ui->pB_Trigger->setEnabled(false);
    // 	ui->dBox_reclen->setEnabled(true);
    // 	ui->dBox_pretrg->setEnabled(true);
    // 	ui->pB_trg_mask->setEnabled(true);
    // 	if (trigger_timer->isActive() && !mWaveDump->NDevStarted)
    // 		trigger_timer->stop();
    // }

}

void ControlPanel::SetStartStopStatus(int status)
{
  pbStartAcq->setEnabled(status == 0);
  pbStopAcq->setEnabled(status == 1);
  // if ((status == 1 && ui->pbStartAcq->isEnabled()) || (status == 0 && ui->pbStopAcq->isEnabled())) { 
  // 	toogleStartStop();
  // }
}



void ControlPanel::UpdateStartableDevices()
{
  pbStartAcq->setEnabled(true);
  
  // int idx;
  // ui->cbDev->blockSignals(true);
  // switch (cmd) {
  // case 0: // remove
  // 	idx = this->ui->cbDev->findText(name);
  // 	if (idx != -1) {
  // 		this->ui->cbDev->removeItem(idx);
  // 	}
  // 	if ((this->ui->cbDev->count() == 1) && (this->ui->cbDev->currentText() == "ALL")) {
  // 		this->ui->cbDev->clear();
  // 		this->ui->cbDev->setEnabled(false);
  // 		this->ui->pbStartAcq->setEnabled(false);
  // 	}
  // 	break;
  // case 1: // add
  // 	this->ui->cbDev->addItem(name);
  // 	if ((this->ui->cbDev->count() > 1) && (this->ui->cbDev->findText("ALL") == -1)) {
  // 		this->ui->cbDev->addItem("ALL");
  // 		this->ui->cbDev->model()->sort(0);
  // 	}
  // 	this->ui->cbDev->setEnabled(true);
  // 	this->ui->pbStartAcq->setEnabled(true);
  // 	break;
  // }
  // ui->cbDev->blockSignals(false);

}



// Adapted for R5560: use Qt on Linux as well; keep the original panel layout.
bool ControlPanel::GetLocalIP(char* outip)
{
  QStringList ips;
  for (const auto &address : QNetworkInterface::allAddresses())
    if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
      ips << address.toString();
  const QByteArray value = ips.join("  ").toUtf8();
  qstrncpy(outip, value.constData(), 1024);
  return !value.isEmpty();
}
