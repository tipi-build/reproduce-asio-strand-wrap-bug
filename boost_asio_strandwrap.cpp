#include <iostream>

#include <boost/config.hpp>

#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/config.hpp>
#include <boost/core/addressof.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/system/error_code.hpp>
#include <boost/type_traits/decay.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/system/error_code.hpp>
//#include <bp/sys/boost/asio.hpp>

namespace user_code {
template<typename ProxiedHandler>
class handler_proxy_t {

public:
  using type = handler_proxy_t<ProxiedHandler>;
  using proxied_handler_t = ProxiedHandler;

  explicit handler_proxy_t(ProxiedHandler h) noexcept(
    std::is_nothrow_move_constructible_v<ProxiedHandler>)
    : h_(std::forward<ProxiedHandler>(h))
  {}

#define ASIO_HANDLER_INVOKE 1
#if ASIO_HANDLER_INVOKE
  template<typename Function>
  friend void asio_handler_invoke(Function&& f, handler_proxy_t* self) {
    using boost::asio::asio_handler_invoke;
    asio_handler_invoke(std::forward<Function>(f), boost::addressof(self->h_));
  }
#endif

  friend bool asio_handler_is_continuation(handler_proxy_t* self) {
    using boost::asio::asio_handler_is_continuation;
    return asio_handler_is_continuation(::boost::addressof(self->h_));
  }

  constexpr ProxiedHandler& Handler() & noexcept {
    return h_;
  }
  constexpr ProxiedHandler&& Handler() && noexcept {
    return std::move(h_);
  }
  constexpr const ProxiedHandler& Handler() const& noexcept {
    return h_;
  }
  constexpr const ProxiedHandler&& Handler() const&& noexcept {
    return std::move(h_);
  }
private:
  ProxiedHandler h_;
};

}


namespace boost::asio {
namespace detail {

  template <typename> struct my_void_type
  {
    typedef void type;
  };

  template <typename T>
  using my_void_t = typename my_void_type<T>::type;


  template <typename T, typename = void>
  struct has_proxied_handler : false_type
  {
  };

  template <typename T>
  struct has_proxied_handler<T, my_void_t<typename T::proxied_handler_t>>
      : true_type
  {
  };

  template<typename Handler, typename Executor>
  class associated_executor_impl< Handler, Executor,
    my_void_t< enable_if_t<has_proxied_handler<Handler>::value> >
  > : associator<associated_executor, Handler, Executor>
  {
  public:
    using type = typename associated_executor<typename Handler::proxied_handler_t, Executor>::type;
    template<typename... Args>
    static auto get(Handler proxy, const Args&... args)
      noexcept
    {
      return asio::get_associated_executor(proxy.Handler(), args...);
    }
  };
}  // namespace detail
} // boost::asio


typedef void CallbackSignature(boost::system::error_code);

template<class CompletionHandler>
class State {
  public:
  typedef CompletionHandler CompletionHandlerType;
  boost::asio::ip::tcp::socket* stream;
  std::function<void()> after_write;
  CompletionHandler            handler;

  State( 
    boost::asio::ip::tcp::socket* stream,
    std::function<void()> after_write,
    CompletionHandler            handler)
    :
    stream(stream),
    after_write(after_write),
    handler(handler)
  {}

};

template<class StateT>
class WriteOp : public user_code::handler_proxy_t<typename StateT::CompletionHandlerType&> {
  public:
  using StatePointerType =  boost::shared_ptr<StateT>;
  typedef user_code::handler_proxy_t<typename StateT::CompletionHandlerType&> Base;
  StatePointerType state_;


  class AfterWriteOp : public Base {
    public:
    StatePointerType state_;
    AfterWriteOp(StatePointerType state) BOOST_NOEXCEPT
      : Base  (state->handler),
        state_(boost::move(state)) 
    {
    }

    void operator()() {
      StateT& state = *state_;
      state.after_write();
    }
  };


  explicit WriteOp(StatePointerType state) BOOST_NOEXCEPT
    : Base  (state->handler),
      state_(boost::move(state))
  {}

  void operator()(boost::system::error_code ec,
                  std::size_t bytes_transferred)
  {
    assert(state_);
    (void)bytes_transferred;
    AfterWriteOp op(boost::move(state_));
    op();
  }

  void operator()() {
    assert(state_);
    StateT& state = *state_;
    boost::asio::async_write(*(state.stream),
                                boost::asio::buffer("SOME DATA"),
                                //state.strand_.wrap(std::move(*this));
                                std::move(*this));
  }

};

template<typename AsyncStream,
         typename AfterWriteInitiatingFunction,
         typename CompletionToken>
BOOST_ASIO_INITFN_RESULT_TYPE(CompletionToken,
                                 CallbackSignature) AsyncCallback(AsyncStream& stream,
                                                                   BOOST_ASIO_MOVE_ARG(AfterWriteInitiatingFunction) after_write,
                                                                   BOOST_ASIO_MOVE_ARG(CompletionToken) token)
{
  auto init = boost::asio::async_completion<CompletionToken, CallbackSignature>(token);
  using HandlerType = typename boost::asio::async_completion<CompletionToken, CallbackSignature>::completion_handler_type;

  typedef State<HandlerType> StateType;
  auto state = boost::make_shared<StateType>(boost::addressof(stream), 
                                                  BOOST_ASIO_MOVE_CAST(AfterWriteInitiatingFunction)(after_write),
                                                  BOOST_ASIO_MOVE_CAST(HandlerType)(init.completion_handler));
  WriteOp<StateType> write_op(state);
  write_op();
  return init.result.get();
}


class Client;
class Initiate;


class Client {
  public:
  boost::asio::io_service::strand strand_;
  boost::asio::ip::tcp::socket stream_;
  Client(boost::asio::io_service& ios) 
    : strand_(ios),
      stream_(ios)
  {} 

  void Start();

  std::function<void()> AfterWrite = [this]() {
    std::cout << "running in this thread : " << strand_.running_in_this_thread() << std::endl;
    assert(strand_.running_in_this_thread());
  };
};

class Initiate {
  public:
  explicit Initiate(Client* self) BOOST_NOEXCEPT
    : self_(self)
  {
    assert(self_);
  }
  void operator()() {
    std::cout << "running in this thread : " << self_->strand_.running_in_this_thread() << std::endl;
    assert(self_->strand_.running_in_this_thread());

    AsyncCallback(self_->stream_,
                      self_->AfterWrite,
                      //boost::asio::bind_executor(
                      //              self_->strand_,
                      //              *this));
                      self_->strand_.wrap(*this));
}
  Client* self_;
};

 void Client::Start() {
  strand_.dispatch(Initiate(this));
}






int main(int argc, char** argv) {
  boost::asio::io_service ios;
  boost::system::error_code ec;
  boost::asio::ip::address addr = boost::asio::ip::address::from_string("127.0.0.1",
                                                                              ec);
  boost::asio::ip::tcp::endpoint ep(addr,
                                       0);
  boost::asio::ip::tcp::acceptor acc(ios);
  acc.open(ep.protocol());
  acc.bind(ep);
  acc.listen(1);
  ep = acc.local_endpoint();  

  std::ostringstream ss;
  ss << ep;
  std::string ep_str = ss.str();
  boost::asio::ip::tcp::socket socket(ios);

  bool started = false;
  ios.post([&] { started = true; });
  acc.async_accept(socket,
                   [](boost::system::error_code ec) {
                    std::cout << "async_accept completed: " << ec << std::endl;
                   });
  Client cli(ios);
  cli.Start();

  size_t n = 0;
  do {
    n = ios.poll();
    if (ios.stopped()) {
        ios.restart();
    }
  } while (started && n == 0u);
  

  return 0;
}
