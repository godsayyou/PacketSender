/*
 * This file is part of Packet Sender
 *
 * Licensed GPL v2
 * http://PacketSender.com/
 *
 * Copyright Dan Nagle
 *
 */

#include "packetnetwork.h"
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDir>
#include <QSettings>

#include "persistentconnection.h"

PacketNetwork::PacketNetwork(QWidget *parent) :
    QTcpServer(parent)
{
}


void PacketNetwork::kill()
{
    udpSocket->close();
    tcpSocket->close();
    close();

    QApplication::processEvents();
    udpSocket->deleteLater();
    tcpSocket->deleteLater();
    QApplication::processEvents();

}

void PacketNetwork::incomingConnection(qintptr socketDescriptor)
{
    QDEBUG() << "new tcp connection";

    TCPThread *thread = new TCPThread(socketDescriptor, this);
    connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));

    QDEBUG() << connect(thread, SIGNAL(packetReceived(Packet)), this, SLOT(packetReceivedECHO(Packet)))
             << connect(thread, SIGNAL(toStatusBar(QString,int,bool)), this, SLOT(toStatusBarECHO(QString,int,bool)))
             << connect(thread, SIGNAL(packetSent(Packet)), this, SLOT(packetSentECHO(Packet)));

    thread->start();

}

void PacketNetwork::packetReceivedECHO(Packet sendpacket)
{
    emit packetReceived(sendpacket);
}

void PacketNetwork::toStatusBarECHO(const QString &message, int timeout, bool override)
{
    emit toStatusBar(message, timeout, override);

}

void PacketNetwork::packetSentECHO(Packet sendpacket)
{
    emit packetSent(sendpacket);

}

void PacketNetwork::init()
{

    udpSocket = new QUdpSocket(this);
    tcpSocket = new QTcpSocket(this);

    receiveBeforeSend = false;
    delayAfterConnect = 0;

    tcpthreadList.clear();

    QSettings settings(SETTINGSFILE, QSettings::IniFormat);

    int udpPort = settings.value("udpPort", 55056).toInt();

    qDebug() << __FILE__ << "/" <<__LINE__ << "udpSocket bind: " << udpSocket->bind(QHostAddress::Any, udpPort);

    int tcpPort = settings.value("tcpPort", 55056).toInt();

    qDebug() << __FILE__ << "/" <<__LINE__ << "tcpServer bind: " << listen(QHostAddress::Any, tcpPort);


    sendResponse = settings.value("sendReponse", false).toBool();
    responseData = (settings.value("responseHex","")).toString();
    activateUDP = settings.value("udpServerEnable", true).toBool();
    activateTCP = settings.value("tcpServerEnable", true).toBool();
    receiveBeforeSend = settings.value("attemptReceiveCheck", false).toBool();
    persistentConnectCheck = settings.value("persistentConnectCheck", false).toBool();

    if(settings.value("delayAfterConnectCheck", false).toBool()) {
        delayAfterConnect = 500;
    }



    if(activateUDP)
    {
        QDEBUG()<< "signal/slot datagram connect: " << connect(udpSocket, SIGNAL(readyRead()),
            this, SLOT(readPendingDatagrams()));

    } else {
        QDEBUG() << "udp server disable";
    }


    if(activateTCP)
    {


    } else {
        QDEBUG() << "tcp server disable";
        close();
    }

}

//TODO add timed event feature?


int PacketNetwork::getUDPPort()
{
    if(activateUDP)
    {
        return udpSocket->localPort();
    } else {
        return 0;
    }
 }

int PacketNetwork::getTCPPort()
{
    if(isListening())
    {
        return serverPort();
    } else {
        return 0;
    }

}


void PacketNetwork::readPendingDatagrams()
{
    //QDEBUG() << " got a datagram";
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;

        udpSocket->readDatagram(datagram.data(), datagram.size(),
                                &sender, &senderPort);

        QDEBUG() << "data size is"<<datagram.size();
//        QDEBUG() << debugQByteArray(datagram);

        Packet udpPacket;
        udpPacket.timestamp = QDateTime::currentDateTime();
        udpPacket.name = udpPacket.timestamp.toString(DATETIMEFORMAT);
        udpPacket.tcpOrUdp = "UDP";
        udpPacket.fromIP = sender.toString();
        udpPacket.toIP = "You";
        udpPacket.port = getUDPPort();
        udpPacket.fromPort = senderPort;

        QDEBUGVAR(senderPort);
//        QDEBUG() << "sender port is " << sender.;

        udpPacket.hexString = Packet::byteArrayToHex(datagram);
        emit packetSent(udpPacket);

        if(sendResponse)
        {
            udpPacket.timestamp = QDateTime::currentDateTime();
            udpPacket.name = udpPacket.timestamp.toString(DATETIMEFORMAT);
            udpPacket.tcpOrUdp = "UDP";
            udpPacket.fromIP = "You (Response)";
            udpPacket.toIP = sender.toString();
            udpPacket.port = senderPort;
            udpPacket.fromPort = getUDPPort();
            udpPacket.hexString = responseData;

            udpSocket->writeDatagram(udpPacket.getByteArray(),sender,senderPort);
            emit packetSent(udpPacket);

        }

        //analyze the packet here.
        //emit packet signal;

    }
}


QString PacketNetwork::debugQByteArray(QByteArray debugArray)
{
    QString outString = "";
    for(int i = 0; i < debugArray.size(); i++)
    {
        if(debugArray.at(i) != 0)
        {
            outString = outString + "\n" + QString::number(i) + ", 0x" + QString::number((unsigned char)debugArray.at(i), 16);
        }
    }
    return outString;
}


void PacketNetwork::disconnected()
{
    QDEBUG() << "Socket was disconnected.";
}

void PacketNetwork::packetToSend(Packet sendpacket)
{

    sendpacket.receiveBeforeSend = receiveBeforeSend;
    sendpacket.delayAfterConnect = delayAfterConnect;
    sendpacket.persistent = persistentConnectCheck;

    if(sendpacket.persistent && (sendpacket.tcpOrUdp.toUpper() == "TCP")) {
        //spawn a window.
        PersistentConnection * pcWindow = new PersistentConnection();
        pcWindow->sendPacket = sendpacket;
        pcWindow->show();
        return;

    }

    QString hashAddress = sendpacket.toIP + ":" + sendpacket.port;
    if(tcpthreadList[hashAddress] != NULL) {
        if(tcpthreadList[hashAddress]->isRunning()) {
            tcpthreadList[hashAddress]->sendAnother(sendpacket);
            return;
        }
    }

    QHostAddress address;
    address.setAddress(sendpacket.toIP);


    if(sendpacket.tcpOrUdp.toUpper() == "TCP")
    {
        QDEBUG() << "Send this packet:" << sendpacket.name;


        TCPThread *thread = new TCPThread(sendpacket, this);

        QDEBUG() << connect(thread, SIGNAL(packetReceived(Packet)), this, SLOT(packetReceivedECHO(Packet)))
                 << connect(thread, SIGNAL(toStatusBar(QString,int,bool)), this, SLOT(toStatusBarECHO(QString,int,bool)))
                 << connect(thread, SIGNAL(packetSent(Packet)), this, SLOT(packetSentECHO(Packet)));
        QDEBUG() << connect(thread, SIGNAL(destroyed()),this, SLOT(disconnected()));

        tcpthreadList[hashAddress] = thread;
        thread->start();
        return;
    }


    QApplication::processEvents();

    sendpacket.fromIP = "You";
    sendpacket.timestamp = QDateTime::currentDateTime();
    sendpacket.name = sendpacket.timestamp.toString(DATETIMEFORMAT);

    if(sendpacket.tcpOrUdp.toUpper() == "UDP")
    {
        sendpacket.fromPort = getUDPPort();
        QDEBUG() << "Sending data to :" << sendpacket.toIP << ":" << sendpacket.port;
        QDEBUG() << "result:" << udpSocket->writeDatagram(sendpacket.getByteArray(), address, sendpacket.port);
        emit packetSent(sendpacket);
    }

}

void PacketNetwork::newSession()
{


}
