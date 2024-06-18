# Since the removal of the `asio_handler_invoke` hook io_context::strand-wrapped intermediate completion handlers do not execute on the strand anymore

After removing code that was working thanks to asio_handler_invoke ADL hooks we started seeing failing assertions on `running_in_this_thread()`. 

The documentation of Boost.Asio informed a while ago already users of the deprecated asio_handler_invoke ADLs hooks to change towards associated_executor traits to perform similar tasks.

Since Boost 1.84.0 asio_handler_invoke ADL hooks were completely removed. We noticed by transitioning a codebase that was heavily dependent on it, from Boost.Asio 1.81.0 to Boost.Asio 1.85.0 that the abstraction does not always easily get replaced and we believe that (the deprecated) `strand.wrap` has been broken by the removal. 

The [code here](./boost_asio_strandwrap.cpp) used to reproduce the issue has a `handler_proxy_t` type to proxy calls to actual handlers, this was used in C++03 code to centralize code for all Asio ADL hooks among other things. It is an highly simplified + anonymized version of production code.

Intermediate completion handlers were built thanks to this idiom and the ADL hooks with base class relationship would be called to handle proxying the call to the underlying handlers.

It happens that this Base class relationship doesn’t play well when switching from asio_handler_invoke to associated_executor, in the example we use an hack that is used to define the associated_executor traits for the proxied handlers to their respective executors ( I will make another issue about this hack ).  

After converting the code to remove asio_handler_invoke in favor of associated_executors (not shown here), data races started to show and assertions checking on `io_context::strand.running_in_this_thread` to trigger, while it did previously work.

It happens that in the past, `asio_handler_invoke_helpers::invoke` was called when handler_work did not detect any owning executor or IoExecutor ( see https://github.com/chriskohlhoff/asio/blob/7609450f71434bdc9fbd9491a9505b423c2a8496/asio/include/asio/detail/handler_work.hpp#L518-L528 ) and thanks to code in `asio::wrapped_handler` redirected work to the strand of the strand-wrapped handler. 

Indeed when the completion handler was strand-wrapped ( i.e. `io_context::strand.wrap()` ) `asio::detail::wrapped_handler` had an `asio_handler_invoke` overload which would instantiate a `rewrapped_handler` and set it's dispatcher to the strand :
  - See former [built-in `asio_handler_invoke` overload for wrapped_handler before asio-1-30-2 / Boost 1.84.0](https://github.com/chriskohlhoff/asio/blob/asio-1-28-2/asio/include/asio/detail/wrapped_handler.hpp#L242-L244)

Together with the removal of `asio_handler_invoke` this built-in logic in `asio::detail::wrapped_handler` was removed as well and in my understanding so far breaks `io_context::strand.wrap()`, which leads to the strand guarantee of sequentialized execution to not be respected anymore where it was before. Now instead of calling via the strand asio ends up [performs a plain call to the function](https://github.com/chriskohlhoff/asio/blob/asio-1-30-2/asio/include/asio/detail/handler_work.hpp#L466-L469) and doesn’t go through the rewrapping anymore which was previously resulting in dispatching to the strand. 

`strand::wrap` is marked deprecated, but the behaviour changed due to these missing asio-builtin ADL hooks. As it is not yet removed and only visible at runtime and difficult to spot there might be interest in documenting this or fixing it.

If one migrates `strand.wrap` calls to `bind_executor(strand)` the issue gets solved and `running_in_this_thread()` returns true again as the call is directly dispatched on the strand as the strand is put as owning executor of the `asio::detail::handler_work` object ( The documentation for `strand.wrap` could possibly point to `bind_executor(strand)` as migration possibility ).

It seems that where before strand guarantee would be provided transitively by checking the completion_token dispatcher, this doesn’t happen anymore.

My understanding of the issue so far is that strand wrapped completion handler doesn't have the strand as bound executor by default, which perhaps could be a solution ?

This might also be the intended behaviour and I might just be seeing a bug where there isn't, but here is an example that can be compiled in both versions of asio and with the following example we can see that when asio_handler_invoke is implemented the intermediate completion handler call to `assert(strand_.running_in_this_thread());` pass, while otherwise fails.

( Boost.Asio versions don't really matter, the problem is fully reproducible in older versions of asio, but it's impossible to get the working state by using `strand.wrap` in newer versions as the built-in asio_handler_invoke doesn't exists anymore ) 

### [Boost 1.82.0 with asio_handler_invoke implemented (-DASIO_HANDLER_INVOKE=1) passes `assert(strand_.running_in_this_thread());`](https://godbolt.org/#z:OYLghAFBqd5TKALEBjA9gEwKYFFMCWALugE4A0BIEAZgQDbYB2AhgLbYgDkAjF%2BTXRMiAZVQtGIHgBYBQogFUAztgAKAD24AGfgCsp5eiyahUAUgBMAIUtXyKxqiIEh1ZpgDC6egFc2TEAtydwAZAiZsADk/ACNsUkCATgAOcgAHdCViFyYvX39A9MzsoTCI6LY4hIsU%2B2xHZyERIhZSIjy/AKCHbCcc5taiMqjY%2BKTUpRa2joLuqaHwkcqxmuSASnt0H1JUTi5LAGZw1F8cAGozA48XSdJsdkvcMy0AQWe3iyOmE59zy48YuhMkQAPQYJh0YAAOiQaTSj3eiM%2Bx1O2AuV0BwJBLCy6GxSiU6FQBBYRGwmAA%2BhJ6ETSWQYXCEa9Dijfmj/pjJviXPjCcTSeSKdh1L0fCRSAz4Qcnszkd9UeiAUCuTieTiAJ7fCl3JQ%2BehESVMj5fH5/DHK0Gq9CG6VIk0KjkW7l4mI%2BGg0eI2mXG1lmpVYq0g4WixpMCngsnqA2wqXelnytmKzmW3Eglzh%2BTC6OM22y%2B2Jx0B1PplSkABuBF2XrtvvZ5qLPIIaWxmEwOqU1bztaTTsDSGMmEYpAzwnCPlJOQpSCBAGtOz6E37k86082iKg0vP46a6/6VcXm79NzGjduHfWueDIVu5Tue1iMHcW23sAT0DQb/ml07H9g08AmDIbAKR8JgfBUTBP27QtLyAkFAO%2BdA0nVFgYkYKDF13Zc2BYGdgKUfs7kgk9cwXO8YNBAjWkFNIiAlEi41vc890o9VJmwNgg1IUgyAzHAMPIi9QSIdU0mAuiWGIJQQRwcR1S3LtMPvfdG3QClSwrKsGJrJSKJXJsQXXY8c0Yr8sN7VNbgHATmOXQMDJYVt2xsgshP0tcNxc78HzghCMGQ1D0O0xTBJYkElDYslOPiHjhwwfjgreV5WA4JQ0hYXYznA%2BI%2BPZAB2GxXiitIjDJf4RLElK0VUHj1AIckAAkByHI0ThxJQzn7JhBxytJavVCkiAuArEVeNIfDQysQHeM4sqyEwzgqusABFOua3r%2BsG/4avQOrGvW0gEQOQqXlm8DwmAM4%2Bt2%2BrKS6nrhyGy5Vp2vbMCa7qWuO0bTrOYUSsrYg1s%2BjbdoGogIFe26Poezq1jOQDhV2WiIBm2azkmTAQBAAglApQCiCQHiAHcKTYdAy2A8Fbh8Po0OAstttq6GDseNY0dmkBOopCBMexwRSGJ1pMCZm79pBw7pQgJA1nZ15ZrMEa8uWn6WRoM4XhEABJAB5CkGpeSJlpCXAACUKS1yIADUdYAaW9WbitK7BytE5h2DRAAxUC%2BiEI1ZpoUh6u6s4y3QAhMDOK0pwOilwjDvCIG975Q0sAA2dOzhochgYeilrvUcGACoMfqGh4cVk70bmi6zmTbGrQb3EY4luOmATl3vvl9Ho/uoc2473miCxkABaF0gRauZPfaYR5aA2OuLQbpzX0JGhebLgBaR4kApWXLir4aVbzdwCBoH6A6D9xF%2B8KPm77nLcZHZwwInIRpdjgvi9L%2Bhy%2BGw%2BzrzUuvXEAjdQH31jk/CMY436zy7r9WadwiDbCYHfdMD9hxQPkDA0MEBsYgMci%2BN868HA0G3tKXe%2B94EK2VhfM4UNxaw13gfREtD4GsOPklLhVU0oZTRAQ3E/93g8PSplHALQGBCOZN3J2ApFRLSqo8DGdFaZDTYANMOEdBpuzRpXDmi03Y4HVpoyOS0WHd0VirdhMiOIlTka7SqHszgABV/Y1wWuoikJjBrolWgopxnjvFmKuK46U2NgknTobIsk8i3ZVRcTnfxHBfGh3DpPB2yjSCqLWnjAut0W6wy5jQCQKhtFiV0SNCxbDIk2LYHYmJDj3bJNCRkmmTgcn52ZoKDB/xnE50CWkraVxZIsHBo0%2BJzjsZ5O6bHIgR0nikWrpzRaWTxI6KqYfSx5iuGO1sc7cZTiYZDkSXEpxuARSoDFGQNxbUCR3z5CSMklJgyXPFHHOp9B/hnCOfEHO5yQxkHIPogZWi5lXD%2BqwemccaBDI8P2XJXS7qsyuD8yWuBsZlgkD4TuuB0QZKUVzdqRJHnXKuES/kTyhQXKuRQb5B0/nUvFG4vR3dxqTVQNNbugDa5LRSUktE5LHmCheTS/4IyxlXH5XSiWUzEUFP7mC3ADKAVovCTo6h6Nomdw8PyqEeqNakGAEoNxs1JgTlQFHK5ZxgDYAhtTIaqLM5fxzvag1Rr056qhFHQ1Sg5YIPRojdQyM5lcqkf6xBtqUFoPQNjG1RAqRvgpcKxlZAIBfxhBSHOrQjWevZhqmhnCC3bMsbNEEIIEYe14WI21klPnMmVmcMtN9JhNxcKrIqhjsDGLSWcDw1IYgZRnCIAgAFSTbGwBAEBEV2JsGxjFXi8VsB5pqS8LV/xbkdS8B821ORHWkQ3WcAYDTKm/TZfQKaaMlpGN7egbdoZUU3rvbug6zj1WHwES4bGTZwkbmxnyPCRAS63HuGwbZprh78x9qnK4JiIB5txSwGgZJhzEyDmVDVW6So7qEA%2BpZeGekasxPQM4GAn1CG1PcVASByQpOKfQFQxbu5HonRcUNH6Y04zSD%2BrjIB/22qA3REDQLQ181HlBnI/xYPwajkhnKqHiDYGE/6zDjB70HTw/hg6frq6cv9cB9gQ87jsA2PoxDyGKTybJBAMzcm0NLqU9XDBH8JbaaEbQ6Rbxqk/TXVcA9zGWnvAPQAdTszrNIZwuZnsrHNHKi7sYYM6WDWFUr/PYxU9hpgqLX3lIsBnaUYbZpRY5Wjc6C1mOqHDsIeI2WVqzSnYRGidF/j%2BaOofK9XaYtxSwJwEACWv7JdOck1LIB0tqYljV9OSirA4k7ofcrlXkM1eUQKCkjGdkkaMHcl4snSAhYU2FiLV0JrnotdNlQBX0ZFd09Xeb4RFtu2W2SVb%2BaNY7b22SMLEBbtVdIEts1ZJ4ZWB1jrEQziKSRB1rgAAGh4XAqhXGhuWWdtEZwh4CnIbgDBJnEfo3%2B8BSdS8QDk0pmjgHFdQ0sv9Vsjz1cTFnCQvEOkpA4NwYuzdloZJXG5ce7VouePnsANxxz7AUIbMobs3BsDSz9GzTPqjvHUJSNYdDBRjK1HMAVxPRpnnMIDqS5e0Win7nfpbOset/6J2gbvewJ97792xI88B8D0H4PIcw7hwj/1XNkezVJ9gDHWOHNLP5wT4E2NicTrx/vDZ1P1t04Z6QJnLOp2RQ4nO7iC7ut/VQEH7X2vRNZAAF7iTruqMkeMJJMCUB6bi5JXOU%2Bru1eIEN%2BfLv0aJjAYpFRfMsBYXoXNe/d/Bb0IfHgLgWAsGcTe4/J//C%2BTEMvr5BqJ6rzXoio/lEj3cJ8l7EATFrAX%2BX5fxhq8xXJFL2a23kPW4O0hUPLaicU0j8LvebfQ137fyb43aN49iUT%2BKFnTXQXJvNoP3PeC/Q9YXLnNOHnFJPnF/CA9jJuTUVACzCXIuP3KEfTNgbHcNPPfA7XJAkAV0d0eIVGCfEQHWAAWVxWWheGcQ%2BAsFwIIJYLz1EwjwgCLkJlxioU2W/3rSsRXR8x1UGzRBeAim%2BGaCMzYFzzwylSv3iGty1iYGyAnBMGnlDFkKWSlVGxyGcXQDwjgW9CBxBzB01l1gtkiC1mcU9kiApFNlwBEAUBCDB2cQAE1VBcAIBdChB9DDCtDWDAje1%2B1B1h1R1kE7h4ZxCUC%2B16B6AB1UAZwIBojJDBMHhudsCAigjsicjWCTCXdzC9YqCdYrZcAKQXhTYABxZIt7OzZQ1Ql%2BYADQnIeGMXNAhTLI3Iro7o9GfIsw7WIokosoio6onwpgPw5geGEgQwv1BvS1EgM4cIIGZ6ZtIgVtDjDULUJXVTCTK4MYiYpgHOWI%2BI0IkdVgCInFCAaYyYqXUrS6LLB7FYqVIgzY1A7YjLf4fYgw5gI4kIxIsI848dR4bGd4lXBLCJKJTtdWZjf4B48pfLfzN9NGFgK1PHFJEBHCPCdSBrSeDwRE%2BErwghFeYhQzEDBeGXHoyk2aPo8ogYikYo0oikDwTWZxGo6/OolQ5wNQpo8TIQNYazHbdogHToqkromkwo%2BkoYpklkiAOEpdCAJYg0UEycLHT/WaG/KUPEqA9VXFSzYCO/KPKXPUikD/KXJBKNRUqEHUPUA0ONfXSJfgrhA9Dwc9ZgMFE6A9eork9DFdQLTbTdV04QC7K7NGIgkseITSHrKyEOaMykKXMMnjIyP9IkADTJEDAXNGF04OCGMMtSDSSsF2bnG4cnL3TJAcHmYsgI7AiszIevJWVaOhOnAYUAz/NGUTGgXkuBDwKTJRBQ3bOzFJMwAAVisG4ONSHOWlZzmPAxHk7yeiuB7wnyySYBUIWnCEWiQFxg3KM0jgHwnw31jIpCtNAlXOADbkGk3IryJnuEwCnIXPBVE23wgJAJb0r0pGPJXIunPLHIvJ3LgzVKPmLS8w809M5KFWDOOwvW7gt0BiGi9KFW8MDMAx/j/hpIh2h1h3h30S5lIR5lITrOAIJGbw3l/nAI1Vj1ml/0ZwAP5KAPbwgzQC2HnI8EXIsGXNPMWNQTHO3JvMO0HznwfLLgpAx0PI/NPO/MvN/JvLvJYofIYqfJexfJIphRErfKPPYq/PCAvKfkJj/N4LoUvwkNQGOISKSNwtUukMzQpOyPMseD7OtxFNYNsulFEtQxYDSE4LHNc26KbVNmwDsUymctwFcsTw8q4MvN4LOGJmICQA3LREEDiPQGioWmsqCKbQHDLJDmAHHBXzJDRGnHoEwA6hYGAEklQQHxx2yKbSIJiHCGeRTWZyCsPPIHCp4NSsCNbIEMzKQqAyEqAsEIvjpyzLdOxmbIhjou7gTO4znW6gyDuwhUwDmuEAgFWPWK/R40IXbH5h4jYHUjogunIIsB4AsDyihC0DOqhB4F7wXmOoOGkCHIAurMVyEAiCcAgHcCWqIE6t%2BlEsIF4XXCQAgHgoFCuIitbMdPbS4XeHmpwnCAVKDOzRzxI0IiLhLmzTLAmt%2BlzPUgjILMWMyHjMJ2nSinT1ilymz0JrD3AU/U42XiIWKqcnRMJzAW/VAWJKUG2tvT2qDhMEOuOtOvOvOquon0ctFLFq6N6AAqmpACTJAA%2BoWz%2Bg8s2tFvFtmluvuqluZtxHWumtAVQGDTICjn1oVNrKlwylQChAZyYHes3GuhIAwHoH/LNv1qwNtRNNohyHvzWOpo41ZtlvNv8vFGxjuGyipHZquNWX0u7nNqhFqu6htoApjvPXYmtp4AAv8pSSTtpHoCFFmoW3tIVhp032xmBB5qNTSLYAxmNQ1TuQErH38qlwL32oWn8u5pSQJCwLontNDK1ppr9t/V4xTNtQxiHohmLLWzqyBGIzNTaBoxWLowYw1RuChAyEmFRhHMmwnP/mW1nsjieNWQPiPkTpdteKpH1sDt5lHpVo02HJsAnK9r/VT1nTlozy63OElrZxyI7yYo3171PoDtohI1vWV3JD3Nn3vProtTruLrls%2BggKCMsQAuGqDJOAIBNt9Sl1QahDGu7qLqLxL1QRWK0Cl0wHQE/sIYOFWmXoyDiNwb0wYrnN/on2oe8EdvhjgQbP3OgYodkrH0fLgZezl3Qc7qQjElvNlk/urmXp1HmDoergoqPiis3MYHl3mDnty0zgoasVWi0B8AArRnNNIFQWIfYWVi4A2HoG4CHP4ACF4H4BjS4BYusFsBHu2EykOD4HIDWK4B0FlnIBnBAAOB4ChDykSDTgsCHK0GSGkC0BOrymkESEMG4GkH4FnUifIFsZ0HIAcf4A5q0C8e0HMfIDgFgBQCVwYF%2BSoAgHKaHDQCMBMB4GSB4HyboH1HiA5snUKfIDjtaHVG4E8Z6dIHVB1hiF0F6G8c8dIw4GEB1iYHoD6Z8f4BwFdGAGOI5rsfIBwBwhMEkA2cIDuD6EpnWayZFTJC6bu3qC6fPRiETyGa8BwH6f4H2tnQ2cplIEBBUGWg4mMGAHPRMEKY2BoCMCNStnqmJjC2YEebkGEDEAkE4BkGhcUBUA0C6f0CHMMB%2BbQFsFsEMAIBiA5tgCqmIItHIDef6eSAsHMY2CQlDHWc3kxmenMGcesB4DyinzoLpINiNhNnNkthtntmeiyaf24gjlfHgA2B6BnjcG6hmACB4GCG6mGAqCqAMFXpKFyG8E6FVeKFDCVdGASHlcldDDGtlYMCNf6HmD1eWANfsHmFNcNctcWGVbGDTs2DcfhYsasZsa6YcbOHUGSDTk3jTmkA2x%2BbOEacurOtR3wGIENo8bWCeYBY2Go0cjGDgySa4BSfIDSfycyfse4FyZAHye8d8Y2ACaHOCbyiHOkGSHibTgraaa0CHMScsa4AOG9cWeyYLYKcWY2BKeQBACYvGiIEoDcEIHFAjgMEEBhfEEkB4AOEReUDUE0E7bRfIDcq417YzesYyZ9e4B1jFGHfp3Vn9cDeDdDYWgjeCa0FR0wwqdIHHwOAesTd7eTZvLTbLZABrahAsHuuOqHLuqaeSESGkHldbazbza7a4ELeLaTYzcpezakC0Fzb3eg57dLdJfaZyC/aAA%3D%3D)

Outputs :
```
running in this thread : 1
async_accept completed: system:0
ec: system:0 - 10
running in this thread : 1
ios.poll() n= 4
```

### [Boost 1.82.0 without asio_handler_invoke (-DASIO_HANDLER_INVOKE=0) crashes on `assert(strand_.running_in_this_thread());` ](https://godbolt.org/#z:OYLghAFBqd5QCxAYwPYBMCmBRdBLAF1QCcAaPECAMzwBtMA7AQwFtMQByARg9KtQYEAysib0QXACx8BBAKoBnTAAUAHpwAMvAFYTStJg1DIApACYAQuYukl9ZATwDKjdAGFUtAK4sGIAMzSrgAyeAyYAHI%2BAEaYxCBmAGwapAAOqAqETgwe3r4B0umZjgKh4VEssfFJKXaYDtlCBEzEBLk%2BfoG2mPYlDE0tBGWRMXEJybbNre35XQpTQ2EjlWM1AJS2qF7EyOwc5v5hyN5YANQm/m5O88SYrBfYJhoAgk%2BvZocMx15nF27RqAyBAA9GgGDRgAA6BCpVIPN4Ij5HE6Yc6XAFA4FMTKoLEKBSoZB4JgETDoAD6YlohJJJGhsPhLwOyJ%2BqL%2BGPmeKceIJRJJZPJmFU9S8RGI9Lh/keTKRXxRaP%2BgM52O52IAnl9ybcFF5aAQJYz3p9vr90UqQSrUAapYjjfL2eaubjol4qFQ4tbpUaWabFZjLcChSK%2BuSwaTVPqYZKvcy5ayFRyLTjgU5Q7IhZGGTaZXb4w7/cnU0piAA3PC7T22n1ss0F7l4VJY9DobUKSs56sJx0BhCGdD0YhpwRhLwk7LkhCAgDW7e9cd9iadKcbBGQqVnsZNNb9ysLjZ%2B66jhs39trnLBEI3sq3XcxaFuTZbmHxqCoV9zC8d98wKeADBImDkl4DBeEo6Dvp2%2BbngBwL/l8qCpGqTDRPQEHztui4sEwU6AQova3OBR7ZnON5QSCeEtAKqQEOKRExtep47uRarzJgLCBsQxAkGmWBoaRZ4ggQaqpIBNFMIQCjAlgohqhuHbobeu71qg5LFmWFZ0VWClkUuDbAquh5ZvRH4Yd2yY3H2fGMYuAZ6UwzatlZeYCbpK5rk5n53jBcFoIhyGoZp8n8UxwIKCxpLsXEXGDmgvGBa8LzMGwCipEwuynKBcQ8WyADsVgvBFqQGKSfxCSJSWosoXGqHgZIABJ9gOhrHNiCinL2DD9llqTVWq5IEOceUIi8qReCh5YgG8pwZZkRinGVNYACLtY13W9f1fxVagNX1atxDwv4%2BXPNNoFhMApw9dttUUh1XWDgNFzLVtO3oA1nVNYdw3HacQpFeWhAre9a3bX1BAQM911vXd7VrKc/5Crs1EQFN02nPM6AgCAeAKOS/4EAgXEAO7kiwqAloBYI3F4DQoYBJabdVkN7Q8awo9NIDteSEDo5j/DEITLToAzV27UD%2B1ShACBrKzLzTSYQ05YtX3MlQpzPEIACSADy5J1c8ESLcE2AAErkhrEQAGpawA0l602FcVmClcJjCsKiABiwENAIhrTVQxC1Z1pwlqgeDoKcloTnt5JhCHOEQJ7Xx9OYiQp6cVCkIDd3kpdqigwAVGjPRULD8tHajM1naciaY5atc4lHYsxwwcdO59suo5Ht0Ds3rfcwQGMgHzAvEELlyJ97DAPNQGzV%2BatcOc%2BBJUNzxcALQPAg5LSxc5eDUrOauHgVBfX7AeuHPngRw33dZdjQ6OCBY4CJL0e5wXRe0CXg17yds3nTXEAdcgE32jvfMMI5n5T3bt9aatwCDbAYNfVMt9BzgNkJAvoEBMaAPsk%2BF8K87BUA3lKLeO8YFy0VqfU4ENRbQy3rvBEVCYFMIPgldhFUUppVRLgnEP83icNSulLAzQ6D8KZB3B2/IFQLQqg8NGNFqYDRYH1EOYd%2BouxRmXNm80XZYFVmo8OC1GEd3lkrFhki2JFWkc7cqbtTgABVfaVzmio8khj%2BpomWrI%2BxbiPHGMuE4qUmMAlHWoVI0kMiXYVUcZnHxbAvHB1DmPO2CjiBKJWjjXO11G7Qw5lQMQSgNEiS0UNUxzCwmWJYNYyJtjXYJKCakqmDhMk50ZgKVBfwHGZz8ckjalxpJMFBnUmJDjMbZI6dHAgB1HjEQruzea6TRKaPKXvMxJj2H2ysY7EZ9ioYDjidE%2Bx2BhTIFFCQZxLV8TX15MSUkFIgxnLFDHaptA/inH2XETOJzgwkFIDo3p6jpmXB%2BswWmMcqD9LcL2LJ7SbrM0uJ88W2BMYljEF4Nu2A0SpPkRzVqhI7kXMuPivk9zBSnPOWQD5e1vkUrFM47RHdRrjWQJNDuf8q4LUSfE1EJK7kCkeZSv4gzhmXB5dSsW4y4W5J7sC7AtLfnIpCZoihqMIltzcDyyE2q1bEGAAoZx015hjmQBHc5pxgCYDBpTAaSK07v0zja3V%2BqU7ashBHPVCgZawNRvDVQiNpnsvET6uBVrEHINQJjS1BBKQvlJQKulJAIDv2hOSTOLR9VutZqqyhbDc0bLMdNYEwI4Zuy4cIq14k3lMkVqcYtl95j1ycMrAqejMAGOSacNwVJohpSnEIPAf4STbEwBAQBYVWIsExlFbisVMDZsqc8dVfwrltQ8K8q12Q7XEVXacAYtSynfWZbQCaKMFr6K7agDdfQkWXuvVuvaDiVV714U4TGDYQlrkxryHCBBC43DuCwDZRqB68y9snS4hiIDZqxUwKgpJByEwDiVVV66iqboELe%2BZ2HOmqoxLQU4aB70CC1HcZACAySJIKbQJQBaO77tHecINr7I1Y1SJ%2B9jIAf1Wv/TRQD/yg08yHuB7IfwoMwYjvBrKSHCCYAEz6tD9Ab17Wwzhva3qK5sp9QB1g/dbisA2DouDCHyQydJBAYz0nkPzvkxXVBr8xYaf4VQiRrwKlfWXZcXdDHGlvF3QAdWs1rVIpwObHvLDNLKc7MaoLaSDKF4qfOY0UxhhgSKn0lKSPIxlR6xontZSjU6c0GPKFDoIOIGWlrTXHfhKiNE/g%2BYOnvc97bIsxQwOwEAsX34JaOQkpLIAUvKbFpVlO8iLDYjbnvErZWEOVYUfyckdHNmEYMNc54UniCBdk8F0LF08sRYm0oYNFdwsFaDTNsIc2XYLdJEtnNatNvbdJMFiAl3yvEHm8a0ksMLBay1kIBx5IIha2wAADTcNgZQTig0LKO6iU4/d%2BQkOwKgwzsPUbfcAmO%2BeIBSbkyRz90uQacsV3Wa5iuhjTgITiLSYg0HoMnfmY1pIt2qv5yx/d3%2BmPmikkhJZxD1noPAfmTo6ax9EdY8hER9DIZ9PkbJKXQ9qm2fQj2sLh7%2BaScue%2BusixK3fr5YBs9zAr33vXZEmz37/3AfA9BxDqHMOfUc3h9NQnmAUdo9s/MznOOgSY3x6OrHO9Vnk5W1TmnxA6cM/HeFNi07OKzo6z9ZA3uVcq6E5kAAXqJauapSQ4zEgwBQ7pOKK9Kdz1qcQwac4XTooTaBRQKneeYMw9QOat%2BbyC%2BoXe3DnDMGYU4a9%2B%2BD7%2BO86I%2Bfnz9Sj8X0vBFe8KMHq4N5D2ICGLWBPgv0/DAl6imSEX00NsIZN7thCfvG147JkH3ngFyHc7P3X8pbCUYR5ElHsUDOleV/xNX9328D97o35OKs5Y6JIc435c46Isb1wajICmZC75zu6Qg6YsDo4hrp4YEq7QEgAuhuhxDIwD5CBawACyWKi0zwDi7wZgaBmBtB6eQmgeEA%2Bc%2BM2Md%2BpSz%2BNa5ii6nmmqfWqIzwYUXwTQ%2BmLAae2G4qR%2BcQJuGsDAWQY4RgE8fQYh8y4qQ22QDiqAOE0CXof2AOQO6s2sZsEQGsDi7sEQ5Ixs2AQgcgwQQODiAAmsoNgBAGoQIBoVocoXQV4V2j2n2gOkOggrcLDAIbAd2rQLQL2sgFOBACEUIXxvcCAfEaIWLt4akWkV4bobbgYTrMQVrBbNgOSM8MbAAOIxFPbWYyFyGPzACKHZCwwC7wGyaeHpEtGtEVyZH6Gaw5F5EFFFGlGuEMDuGMCwxEBaHeqk5mpECnBhAAyPQNoEBNqsbqiagy5KaiaXADFDEMCZxhERF%2BGDrMCBGYoQCjHDEi5FbnTpY3ZzHirYHLFwGrGpZ/CbGaGMA7G%2BFRH%2BGHEjoPCYyPEhixahLhJtqqwMZ/BXElJSiAH8ijaqpMDmqgFzGAJYQ4SqS1ZjxuA%2BYqrOG4KLwEJ6aAazwpFtEkmnAdGFFdHki5H5HkhuDqwOJlHH4VGyGODyE1EiYCBrAWabaNE/bNGkmtHknZFUk9G0n0kQAQnzoQAzH6j/Hjho6P7fQn6SiYlAHYmnBmaARn7B4i6ankgP4i7wLhoymQjai6j6jRoa5hI65fS7puAnqMDApHS7qVGskoaLp%2BZrZroOmCBM5nZabVa47AJFhxDqSdYWRBwRkUgi7YEfogAGTfqEi/ppKAaQEdz2mBxgyxkqRqTlhOys7XDE7O5pJ9hcyFmeEoFlkZBObaK1rUJU4DCtBWnUJCZUAcnQJuDibyKSFbbWaJImAACsFgLBBqA5i0jOExIGg8jeD0lwLeA%2B6SDAshc0YQ80CA2Ma5%2Bm4cHeA%2BC%2BUZ5IppwEy5wAzc/U65heBMdw6AE5c5IKQmy%2BABVeTZ%2B5h5S5Z0p5I5Z5W50Gip%2BaLC7mrmLpLJ/KfpB25230hu/0A0rp/KLhPpf6n8385JIO4OkO0OOiHMRCXMRCNZyuncP%2Bz5xc/%2BqqYe00r%2BtOH%2BXJX%2B9eoGKAWws5bg85Zgi5x50xSCI5m5V5e2neY%2Bd5RFKOL5LF75YQZ598%2BM35pct5fe9570j5BFNe/FDwglR5wlDAolF5ElipOisRyAuxkR0RWFAlSRaaxJ3hhlDwPZJu/JXh5lUoL5SGTAqQTBI5TmrR9axsmA1i6Utl2A9lUeTlzB55d%2BGphACAa5qI/A4RqAhMVcplXh9afYJZQcwAo4M%2BpIqIk4tA6AbUTAwA4kSCHeGOqR9a2B0QYQDyia9OPl%2B5pAgVrBcVdBWlnBKMGZjp/6RFBaAFmyVOrVggmMjZYM1FHcsZnGCZIArg6QV2oK6Ak1ggEA8xix76nGeCrYvMXELAqkNEZ0BBZgXAZgOUkIGgh1kIXAres8e1gQA5v5KZrAB5YI4QDgEAE1s2TV30L5%2BAXCq4CAEAMF/IJxQVWlNprmLa7CU1WEYQ0pvpGaqehG%2BE%2BchcGaJYQ1302ZqkoZeZ0xGQMZuOE6EUCe0U2UKe2N/uICb6bGC8%2BCOVDkiSI1FNq1Q861m1AcRgO1e1B1R1R1p1A%2B1lApvNrR9Q11tN8ZX641nUs1A0nlFmDkPNfNqMF1kgV1xNF%2BwZo1ItaUAaJAEcyAyA0p1ZIu6tkINODAT164l0RAaAtAP5%2Bt2tyBVq%2Bp1E2Q5%2BCxpNrGcZY16tnlYomMtwmUlIeJCgJxSybBHcBtZVnUJt11BtJ6rExtXA11nliSUdNItAgoYts2VpcsFOi%2BmMQIzN%2BqSRaMBqqq1yvFfenlIumeW1c0nlTNiS%2BIyBNEzZw1QZOIS1HGiZURVqaMSZVqutXqy2gZV8xqrQlGcx1GtGqq1wkIRQYMg5VgWWY5P8C2I94cNxSyu8%2B8kdNt9xlI2tnt3MPdBAMtKuc9g545sek6%2BNSeZwAtTO6RDe9FC%2BreO9Ht1EhGV6suZIO5o%2BUlKee5tFD5muXhZi11vVA0xweAfd11EDkIA1TdK22eueSCcxGgIu6AqAd9cMiSU96Q4R8DFcD9TepdI%2BODngltsM0Cy0PFv9yDv9Ml/YABEufdDdCEIk150smDFcU92oCw%2BDqMpF%2B8Gp659AkuCwo9WWrOtD5iy0GgXg11KMRpxASCqD/5i0HAGwtAnAA5vAfg3AvAkaHAjFlg1g3d2w6UBwPApACxHAWg0spAU4IAkgZgkIgQAAHP4P4IkAOQAJySDeNcBcC%2BP6CcCSA6OaD6OcC8AKAgApDWO2OkBwCwAwCIB0XVJ0BfIUAQAy7pPxAtRGBcCuNcApA0B6hxDRNjrhOkBh0tBqicCWPVPEBqhazRDaD1DWOWNEZsCCBawMC0C1M2O8BYAujAC7HRN6OkBYBYRGDiDjP4C3ANDkxjNaCkCCqkiVNXY9CVMnrRBR6NMeBYB1O8BbVTrjPkzEAAhKCLRsSGDAAnpGDhMbBUAGD6oWy1SEzBaMCHMyCCAiBiDsBSDfPyBKBqCVO6ADn6A3MoDWDWD6B4DRDROwAVQ4HmikBnN1OuNmDqMbAIR9BjNrzoyPSmDGOWBcA5RD7kGUl6wGxGymzmxWy2yPSWNX6cRhzPjwAbB1CTwuCdQzB%2BBcArOdTDAVBVB6BFBZACC8uisZDisMBCujDxD8uct9ADWSuKs9BtONALBysrAKuTCDCqt6utDasitx2bBmP/MaNaNhMDOkAGOnCqCuOJBryJCSCrY3OnAFMnWHWI64CECa0WNrBHMPMbAOMDkpCaMcChOkBTphukC6PLMGNRMxNWMPMJPJOSwgD0WjRH2ZNkh%2BsBwYz8v8A/OiDiBcD%2BCAuKAqDqA2tgukAOXsYDOWscDaNxuVMGNayijZvU6qwOtOsututzSetcDesuEf05P97%2BBXVBtNsbAUb2RjDQb2MgCxsRtRvxsRMcBJuxPBvLsDkHVeNcCJCJCBAaBhuuOuMaCJDBMcCYvRsSAaApAbu2uRMpuzs3t3tTpFNPvtuvtxNYuotlPZCONAA%3D%3D%3D)

Outputs : 
```
running in this thread : 1
async_accept completed: system:0
ec: system:0 - 10
running in this thread : 0
Assertion failed: (strand_.running_in_this_thread()), function operator(), file boost_asio_strandwrap.cpp, line 221.
```



