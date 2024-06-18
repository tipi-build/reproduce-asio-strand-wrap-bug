# Migrating asio_handler_invoke code relying on base class ADL hooks
While migrating a codebase heavily dependent on the now deleted asio_handler_invoke ADL hooks we noticed that it was impossible to define generically associated_executor/allocator traits for a class hierarchy without using any private implementation detail of asio traits.

Indeed in cases where the ADL hooks were used on Handler base classes, [as you can see here](./boost_asio_strandwrap.cpp) transitioning is either very intensive, inverting the abstraction in user code or requires relying on private traits implementation detail of Boost.Asio.

In principle in our case defining `get_executor()` member functions in the base class could have worked but the issue is that get_executor doesn't get the same parameter list than the static `associated_executor::get(const T & t, const Executor & ex);`. The member function `get_executor` alternative doesn't get passed the builtin executor selected by Asio and we needed to keep track of the Asio provided executor, in order to wrap it and surroung it's post,dispatch... functions with logic that was previously in asio_handler_invoke.

We ended up with the following hack using private asio implementation detail:: namespace, that we think would be great to enable publicly by a change on the public API of the `associated_executor` traits : 

```cpp
namespace boost::asio::detail {
template<typename Handler, typename Executor>
  class associated_executor_impl< Handler, Executor,
    void_t< enable_if_t<std::is_base_of<user_code::handler_proxy_t<typename Handler::proxied_handler_t>, Handler >::value> >
  > : associator<associated_executor, Handler, Executor>
  {
  public:
    using type = typename associated_executor<decay_t<typename Handler::proxied_handler_t>, Executor>::type;
    template<typename... Args>
    static auto get(const Handler& proxy, const Args&... args)
      noexcept
    {
      return asio::get_associated_executor(proxy.h_, args...);
    }
  };
}
```

If the public [asio::associated_executor](https://www.boost.org/doc/libs/1_83_0/doc/html/boost_asio/reference/associated_executor.html) template class was given a third template parameter of the type `typename = void` ( as is actually done in `associated_executor_impl` ) it would allow to migrate such code in a simpler way by using it to partially specialize the traits using SFINAE ( e.g. using `std::enable_if<std::is_base_of ... >` as partial specialization of this parameter ). 

Right now  I don't think it is possible to specialize the traits in a generic way with SFINAE selected template specialization without relying on private implementation details.

Hence the reason for this issue, is there a chance to see a future version of Asio provide the public traits like this : 

```
template <typename T, typename Executor, typename = void>
struct associated_executor;
```

Or is there a good reason not to allow SFINAE at this level ?