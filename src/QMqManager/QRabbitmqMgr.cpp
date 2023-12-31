#include "QRabbitmqMgr.h"
#include <QThread>
#include <QMutexLocker>
#include "amqpcpp.h"
#include "QTcpConnectionHandler.h"


using namespace std;

namespace AMQP_QT {

QRabbitmqMgr::QRabbitmqMgr(const MqInfo &mqinfo, MqRoles role, int hbInterval)
  : QObject(nullptr), m_role(role), m_heartbeatInterval(hbInterval)
{
    this->Init(mqinfo);
}

QRabbitmqMgr::~QRabbitmqMgr()
{
    this->ReleaseMqInstance();
}

bool QRabbitmqMgr::Init(const MqInfo &mqInfo)
{
    m_mqInfo = mqInfo;
    // 非消费模式，不使用心跳
    if(!(m_role & MqConsumer) && m_heartbeatInterval != 0) {
        m_heartbeatInterval = 0;
    }

    return true;
}

bool QRabbitmqMgr::StartMqInstance()
{
    try {
        m_pTcpClient = make_shared<QTcpClient>(m_mqInfo.ip, m_mqInfo.port);
        connect(m_pTcpClient.get(), &QTcpClient::sigParseTcpMsg, this, &QRabbitmqMgr::OnParseTcpMessage);
        connect(m_pTcpClient.get(), &QTcpClient::sigSocketErr, this, &QRabbitmqMgr::OnTcpErrHandle);

        // 等待tcp连接建立成功
        if (!m_pTcpClient->NewConnect()) {
            m_errMessag = "Connect MqSever Failed";
            return false;
        }

        // 创建handler对象
        m_pHandler = make_shared<QTcpConnectionHandler>(m_pTcpClient);
        m_pHandler->setHeartbeatInterval(m_heartbeatInterval); //设置心跳
        connect(m_pHandler.get(), &QTcpConnectionHandler::sigStartHeartbeatTimer, this, &QRabbitmqMgr::OnStartHeartbeatTimer);

        // 创建connection对象，开始登录Rmq
        m_connection = std::make_shared<AMQP::Connection>(m_pHandler.get(),
                                                          AMQP::Login(m_mqInfo.loginName.toStdString(), m_mqInfo.loginPwd.toStdString()),
                                                          m_mqInfo.vhost.toStdString());

        // 创建channel
        if(!this->CreateMqChannel()) {
            return false;
        }
    }
    catch (const std::exception &e) {
        m_errMessag = "Init MqInstance Failed: " + QString(e.what());
        return false;
    }

    return true;
}

bool QRabbitmqMgr::PublishMsg(const QString &msg)
{
    if(!(m_role & MqPublisher)) {
        m_errMessag = "Publish Messsage: MqRole is not Publisher";
        return false;
    }
    if(m_channel == nullptr) {
        m_errMessag = "Publish Messsage: channelPub is null";
        return false;
    }

    try {
        //注意: 单通道无法支撑较大的数据流量，多线程发送需要加锁
        QMutexLocker channelLocker(&m_channelMutex);
        QString realRouteKey = m_mqInfo.routingKey.isEmpty()? m_mqInfo.queueName : m_mqInfo.routingKey;
        m_channel->publish(m_mqInfo.exchangeName.toStdString(), realRouteKey.toStdString(), msg.toStdString());
    }
    catch (const std::exception &e) {
        m_errMessag = "Publish Messsage: " + QString(e.what());
        return false;
    }

    return true;
}

void QRabbitmqMgr::ReleaseMqInstance()
{
    try {
        this->CloseMqChannel();
        this->CloseMqConnection();

        if(m_heartbeatTimer) {
            m_heartbeatTimer->stop();
        }
    }
    catch (const std::exception &e) {
        m_errMessag = "Release MqInstance Failed: " + QString(e.what());
    }
}

void QRabbitmqMgr::BindQueueExchange()
{
    if (!CreateMqExchange(m_mqInfo.exchangeName, m_mqInfo.exchangeType)) {
        this->OnPrintErrMsg(m_errMessag);
    }

    if (!CreateMqQueue(m_mqInfo.queueName)) {
        this->OnPrintErrMsg(m_errMessag);
    }

    if (!BindQueue(m_mqInfo.queueName, m_mqInfo.exchangeName, m_mqInfo.bindingKey)) {
        this->OnPrintErrMsg(m_errMessag);
    }
}

QString QRabbitmqMgr::getErrorMessage() const
{
    return m_errMessag;
}

void QRabbitmqMgr::OnStatusChange(const bool isOk)
{
    if (isOk) {
        m_mqConnErrIndex = 0;
        return;
    }

    m_mqConnErrIndex++;
    qCritical() << "QRabbitmqMgr::OnStatusChange, error:" << m_errMessag
                << ", count:" << m_mqConnErrIndex;

    emit sigMqConnectError();
    this->CloseMqChannel();
    m_role = MqRoles::MqNone;
}

void QRabbitmqMgr::OnPrintErrMsg(const QString &err)
{
    qCritical() << __FUNCTION__ << "mq Error: " << err;
}

void QRabbitmqMgr::OnParseTcpMessage(const QByteArray &msg)
{
    if (!m_connection || !m_connection->usable()) {
        return;
    }

    try {
        auto data_size = msg.size();
        size_t parsed_bytes = 0;
        auto expected_bytes = m_connection->expected();
        while (data_size - parsed_bytes >= expected_bytes) {
            QByteArray tmpBUf = msg.mid(parsed_bytes, parsed_bytes + expected_bytes);
            parsed_bytes += m_connection->parse(tmpBUf.data(), tmpBUf.size());
            expected_bytes = m_connection->expected();
        }

        //msg.erase(m_parseBuf.begin(), m_parseBuf.begin() + parsed_bytes);
    }
    catch (const std::exception&e) {
        m_errMessag = "Parse MqData Error: " + QString(e.what());
        this->OnPrintErrMsg(m_errMessag);
    }
}

void QRabbitmqMgr::OnStartHeartbeatTimer(int interval)
{
    qDebug() << __FUNCTION__ << " starts.";
    if(m_heartbeatTimer && m_heartbeatTimer->isActive()) {
        return;
    }

    m_heartbeatTimer = std::make_shared<QTimer>();
    connect(m_heartbeatTimer.get(), &QTimer::timeout, [this]() {
        //qDebug() << "starts send heartbeat, ready: " << (m_connection != nullptr);

        if(!m_connection) { return; }
        m_connection->heartbeat();
    });

    m_heartbeatTimer->start(1000*interval/3);
}

void QRabbitmqMgr::OnTcpErrHandle(const QString &err)
{
    m_errMessag = err;

    emit sigMqConnectError();
    this->OnStatusChange(false);
    this->ReleaseMqInstance();
}

bool QRabbitmqMgr::CreateMqChannel()
{
    if(m_role == MqNone) {
        m_errMessag = "Create Channel Failed: MqRole is None";
        return false;
    }

    try {
        this->CloseMqChannel();

        m_channel = std::make_shared<AMQP::Channel>(m_connection.get());
        // 建立成功时回调
        m_channel->onReady(std::bind(&QRabbitmqMgr::ChannelOkCb, this));
        // 通道发生错误时调用回调函数
        m_channel->onError(std::bind(&QRabbitmqMgr::ChannelErrCb, this, std::placeholders::_1));
    }
    catch (const std::exception &e) {
        m_errMessag = "Create Channel Failed: " + QString(e.what());
        this->OnStatusChange(false);
        return false;
    }

    return true;
}

bool QRabbitmqMgr::CloseMqChannel()
{
    try {
        QMutexLocker channelLocker(&m_channelMutex);
        if (m_channel && m_channel->usable()) {
            m_channel->close().onError(std::bind(&QRabbitmqMgr::ChannelCloseErrCb, this, std::placeholders::_1));
        }
    }
    catch (const std::exception &e) {
        m_errMessag = "Closing Channel Failed: " + QString(e.what());
        return false;
    }

    return true;
}

bool QRabbitmqMgr::CloseMqConnection()
{
    try {
        if (m_connection && m_connection->usable()) {
            bool ret = m_connection->close();
            if (!ret) {
                m_errMessag = "Closing Conncetion Failed";
                return ret;
            }
        }
    }
    catch (const std::exception &e) {
        m_errMessag = "Closing Conncetion Failed: " + QString(e.what());
        return false;
    }

    return true;
}

bool QRabbitmqMgr::CreateMqExchange(const QString &exchangeName, const QString &exchangeType)
{
    try {
        if(exchangeName.isEmpty()) {
            m_errMessag = "ExchangeName is empty.";
            return false;
        }

        if(!MqExTypeMap.contains(exchangeType)) {
            m_errMessag = "Create Exchange Failed, Unknow exType: " + exchangeType;
            return false;
        }

        AMQP::ExchangeType type = MqExTypeMap[exchangeType];
        m_channel->declareExchange(exchangeName.toStdString(), type, AMQP::durable).onError(std::bind(&QRabbitmqMgr::CreatMqExchangeErrCb, this, std::placeholders::_1));
    }
    catch (const std::exception &e)
    {
        m_errMessag = "Create Exchange Failed: " + QString(e.what());
        return false;
    }

    return true;
}

bool QRabbitmqMgr::CreateMqQueue(const QString &queueName)
{
    try {
        m_channel->declareQueue(queueName.toStdString(), AMQP::durable).onError(std::bind(&QRabbitmqMgr::CreatMqQueueErrCb, this, std::placeholders::_1));
    }
    catch (const std::exception &e) {
        m_errMessag = "Create Queue Failed: " + QString(e.what());
        return false;
    }

    return true;
}

bool QRabbitmqMgr::BindQueue(const QString &queueName, const QString &exchangeName, const QString &bindingKey)
{
    try {
        if(exchangeName.isEmpty() || queueName.isEmpty()) {
            m_errMessag = "ExchangeName or QueueName is empty.";
            return false;
        }

        QString realBindKey = bindingKey.isEmpty()? queueName : bindingKey;
        m_channel->bindQueue(exchangeName.toStdString(), queueName.toStdString(), realBindKey.toStdString())
                .onError(std::bind(&QRabbitmqMgr::BindQueueErrCb, this, std::placeholders::_1));
    }
    catch (const std::exception &e) {
        m_errMessag = "Bind Queue Failed: " + QString(e.what());
        return false;
    }

    return true;
}

bool QRabbitmqMgr::SetQosValue(const uint16_t val)
{
    try{
        m_channel->setQos(val).onError(std::bind(&QRabbitmqMgr::SetQosValueErrCb, this, std::placeholders::_1));
    }
    catch (const std::exception &e){
        m_errMessag = "Set Qos Failed: " + QString(e.what());
        return false;
    }
    return true;
}

bool QRabbitmqMgr::StartConsumeMsg()
{
    if(!(m_role & MqConsumer)) {
        m_errMessag = "Consume Data Failed: MqRole is not Consumer";
        return false;
    }
    if(m_channel == nullptr) {
        m_errMessag = "Consume Data Failed: channel is null";
        return false;
    }

    try {
        // 自动ack
        m_channel->consume(m_mqInfo.queueName.toStdString())
            .onReceived(std::bind(&QRabbitmqMgr::OnConsumeRecved, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
            .onError(std::bind(&QRabbitmqMgr::ConsumeErrorCb, this, std::placeholders::_1));
    }
    catch (const std::exception &e) {
        m_errMessag = "Consume Data Failed: " + QString(e.what());
        return false;
    }
    return true;
}

bool QRabbitmqMgr::PurgeMsgQueue()
{
    try {
        QMutexLocker channelLocker(&m_channelMutex);
        if (m_channel && m_channel->usable()) {
            m_channel->purgeQueue(m_mqInfo.queueName.toStdString());
        }
    }
    catch (const std::exception &e) {
        m_errMessag = "Purge Queue Failed: " + QString(e.what());
        return false;
    }

    return true;
}

void QRabbitmqMgr::ChannelOkCb()
{
    this->BindQueueExchange();
    this->OnStatusChange(true);
}

void QRabbitmqMgr::ChannelErrCb(const char *msg)
{
    m_errMessag = "Channel Error: " + QString(msg);
    this->OnStatusChange(false);
}

void QRabbitmqMgr::ChannelCloseErrCb(const char *msg)
{
    m_errMessag = "Close Channel Failed: " + QString(msg);
}

void QRabbitmqMgr::CreatMqExchangeErrCb(const char *msg)
{
    m_errMessag = "Create Exchange Failed: " + QString(msg);
    this->OnStatusChange(false);
}

void QRabbitmqMgr::CreatMqQueueErrCb(const char *msg)
{
    m_errMessag = "Create Queue Failed: " + QString(msg);
    this->OnStatusChange(false);
}

void QRabbitmqMgr::BindQueueErrCb(const char *msg)
{
    m_errMessag = "Bind Queue Failed: " + QString(msg);
    this->OnStatusChange(false);
}

void QRabbitmqMgr::SetQosValueErrCb(const char *msg)
{
    m_errMessag = "Set Qos Failed: " + QString(msg);
    this->OnStatusChange(false);
}

void QRabbitmqMgr::ConsumeErrorCb(const char *msg)
{
    m_errMessag = "Consume Data Failed: " + QString(msg);
    this->OnStatusChange(false);
}

void QRabbitmqMgr::OnConsumeRecved(const AMQP::Message &message, uint64_t deliveryTag, bool redelivered)
{
    Q_UNUSED(redelivered)
    QByteArray tmpData(message.body(), message.bodySize());
    emit sigRecvedDataReady(tmpData);
    //qDebug() << __FUNCTION__ << tmpData;

    // acknowledge the message
    m_channel->ack(deliveryTag);
}



} //namespace AMQP_QT
