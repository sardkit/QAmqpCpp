/**
 *  DeferredConsumer.h
 *
 *  Deferred callback for consumers
 *
 *  @copyright 2014 - 2022 Copernica BV
 */

/**
 *  Include guard
 */
#pragma once

/**
 *  Dependencies
 */
#include "deferredextreceiver.h"

/**
 *  Set up namespace
 */
namespace AMQP {
    
/**
 *  Forward declararions
 */
class BasicDeliverFrame;

/**
 *  We extend from the default deferred and add extra functionality
 */
class DeferredConsumer : public DeferredExtReceiver, public std::enable_shared_from_this<DeferredConsumer>
{
private:
    /**
     *  Callback to execute when consumption has started
     *  @var    ConsumeCallback
     */
    ConsumeCallback _consumeCallback;

    /**
     *  Callback to excute when the server has cancelled the consumer
     *  @var    CancelCallback
     */
    CancelCallback _cancelCallback;

    /**
     *  Process a delivery frame
     *
     *  @param  frame   The frame to process
     */
    void process(BasicDeliverFrame &frame);

    /**
     *  Report success for frames that report start consumer operations
     *  @param  name            Consumer tag that is started
     *  @return Deferred
     */
    virtual const std::shared_ptr<Deferred> &reportSuccess(const std::string &name) override;

    /**
     *  Report that the server has cancelled this consumer
     *  @param  namae           The consumer tag
     */
    void reportCancelled(const std::string &name)
    {
        // report
        if (_cancelCallback) _cancelCallback(name);
    }

    /**
     *  Get reference to self to prevent that object falls out of scope
     *  @return std::shared_ptr
     */
    virtual std::shared_ptr<DeferredReceiver> lock() override { return shared_from_this(); }

    /**
     *  The channel implementation may call our
     *  private members and construct us
     */
    friend class ChannelImpl;
    friend class ConsumedMessage;
    friend class BasicDeliverFrame;

public:
    /**
     *  Constructor that should only be called from within the channel implementation
     *
     *  Note: this constructor _should_ be protected, but because make_shared
     *  will then not work, we have decided to make it public after all,
     *  because the work-around would result in not-so-easy-to-read code.
     *
     *  @param  channel     the channel implementation
     *  @param  failed      are we already failed?
     */
    DeferredConsumer(ChannelImpl *channel, bool failed = false) :
        DeferredExtReceiver(failed, channel) {}

public:
    /**
     *  Register a callback function that gets called when the consumer is
     *  started. In the callback you will for receive the consumer-tag
     *  that you need to later stop the consumer
     *  @param  callback
     */
    inline DeferredConsumer &onSuccess(const ConsumeCallback& callback) { return onSuccess(ConsumeCallback(callback)); }
    DeferredConsumer &onSuccess(ConsumeCallback&& callback)
    {
        // store the callback
        _consumeCallback = std::move(callback);

        // allow chaining
        return *this;
    }

    /**
     *  Register the function that is called when the consumer starts.
     *  It is recommended to use the onSuccess() method mentioned above
     *  since that will also pass the consumer-tag as parameter.
     *  @param  callback
     */
    inline DeferredConsumer &onSuccess(const SuccessCallback& callback) { return onSuccess(SuccessCallback(callback)); }
    DeferredConsumer &onSuccess(SuccessCallback&& callback)
    {
        // call base
        Deferred::onSuccess(std::move(std::move(callback)));

        // allow chaining
        return *this;
    }

    /**
     *  Register a function to be called when a full message is received
     *  @param  callback    the callback to execute
     */
    inline DeferredConsumer &onReceived(const MessageCallback& callback) { return onReceived(MessageCallback(callback)); }
    DeferredConsumer &onReceived(MessageCallback&& callback)
    {
        // store callback
        _messageCallback = std::move(callback);

        // allow chaining
        return *this;
    }

    /**
     *  Alias for onReceived() (see above)
     *  @param  callback    the callback to execute
     */
    inline DeferredConsumer &onMessage(const MessageCallback& callback) { return onMessage(MessageCallback(callback)); }
    DeferredConsumer &onMessage(MessageCallback&& callback)
    {
        // store callback
        _messageCallback = std::move(callback);

        // allow chaining
        return *this;
    }

    /**
     *  RabbitMQ sends a message in multiple frames to its consumers.
     *  The AMQP-CPP library collects these frames and merges them into a 
     *  single AMQP::Message object that is passed to the callback that
     *  you can set with the onReceived() or onMessage() methods (see above).
     * 
     *  However, you can also write your own algorithm to merge the frames.
     *  In that case you can install callbacks to handle the frames. Every
     *  message is sent in a number of frames:
     * 
     *      - a begin frame that marks the start of the message
     *      - an optional header if the message was sent with an envelope
     *      - zero or more data frames (usually 1, but more for large messages)
     *      - an end frame to mark the end of the message.
     *  
     *  To install handlers for these frames, you can use the onBegin(), 
     *  onHeaders(), onData() and onComplete() methods.
     * 
     *  If you just rely on the onReceived() or onMessage() callbacks, you
     *  do not need any of the methods below this line.
     */

    /**
     *  Register the function that is called when the start frame of a new 
     *  consumed message is received
     *
     *  @param  callback    The callback to invoke
     *  @return Same object for chaining
     */
    inline DeferredConsumer &onBegin(const StartCallback& callback) { return onBegin(StartCallback(callback)); }
    DeferredConsumer &onBegin(StartCallback&& callback)
    {
        // store callback
        _startCallback = std::move(callback);

        // allow chaining
        return *this;
    }

    /**
     *  Register the function that is called when the start frame of a new 
     *  consumed message is received
     *
     *  @param  callback    The callback to invoke
     *  @return Same object for chaining
     */
    inline DeferredConsumer &onStart(const StartCallback& callback) { return onStart(StartCallback(callback)); }
    DeferredConsumer &onStart(StartCallback&& callback)
    {
        // store callback
        _startCallback = std::move(callback);

        // allow chaining
        return *this;
    }
    
    /**
     *  Register a function that is called when the message size is known
     * 
     *  @param  callback    The callback to invoke for message headers
     *  @return Same object for chaining
     */
    inline DeferredConsumer &onSize(const SizeCallback& callback) { return onSize(SizeCallback(callback)); }
    DeferredConsumer &onSize(SizeCallback&& callback)
    {
        // store callback
        _sizeCallback = std::move(callback);
        
        // allow chaining
        return *this;
    }

    /**
     *  Register the function that is called when message headers come in
     *
     *  @param  callback    The callback to invoke for message headers
     *  @return Same object for chaining
     */
    inline DeferredConsumer &onHeaders(const HeaderCallback& callback) { return onHeaders(HeaderCallback(callback)); }
    DeferredConsumer &onHeaders(HeaderCallback&& callback)
    {
        // store callback
        _headerCallback = std::move(callback);

        // allow chaining
        return *this;
    }

    /**
     *  Register the function to be called when a chunk of data comes in
     *
     *  Note that this function may be called zero, one or multiple times
     *  for each incoming message depending on the size of the message data.
     *
     *  If you install this callback you very likely also want to install
     *  the onComplete callback so you know when the last data part was
     *  received.
     *
     *  @param  callback    The callback to invoke for chunks of message data
     *  @return Same object for chaining
     */
    inline DeferredConsumer &onData(const DataCallback& callback) { return onData(DataCallback(callback)); }
    DeferredConsumer &onData(DataCallback&& callback)
    {
        // store callback
        _dataCallback = std::move(callback);

        // allow chaining
        return *this;
    }

    /**
     *  Register a funtion to be called when a message was completely received
     *
     *  @param  callback    The callback to invoke
     *  @return Same object for chaining
     */
    inline DeferredConsumer &onComplete(const DeliveredCallback& callback) { return onComplete(DeliveredCallback(callback)); }
    DeferredConsumer &onComplete(DeliveredCallback&& callback)
    {
        // store callback
        _deliveredCallback = std::move(callback);

        // allow chaining
        return *this;
    }

    /**
     *  Register a funtion to be called when a message was completely received
     *
     *  @param  callback    The callback to invoke
     *  @return Same object for chaining
     */
    inline DeferredConsumer &onDelivered(const DeliveredCallback& callback) { return onDelivered(DeliveredCallback(callback)); }
    DeferredConsumer &onDelivered(DeliveredCallback&& callback)
    {
        // store callback
        _deliveredCallback = std::move(callback);

        // allow chaining
        return *this;
    }
    
    /**
     *  Register a funtion to be called when the server cancelled the consumer
     *
     *  @param  callback    The callback to invoke
     *  @return Same object for chaining
     */
    inline DeferredConsumer &onCancelled(const CancelCallback& callback) { return onCancelled(CancelCallback(callback)); }
    DeferredConsumer &onCancelled(CancelCallback&& callback)
    {
        // store callback
        _cancelCallback = std::move(callback);

        // allow chaining
        return *this;
    }
};

/**
 *  End namespace
 */
}
