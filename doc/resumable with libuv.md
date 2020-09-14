Previously on this blog we have talked about Resumable Functions, and even recently we touched on the renaming of the yield keyword to co_yield in our implementation in Visual Studio 2017. I am very excited about this potential C++ standards feature, so in this blog post I wanted to share with you a real world use of it by adapting it to the libuv library. You can use the code with Microsoft’s compiler or even with other compilers that have an implementation of resumable functions. Before we jump into code, let’s recap the problem space and why you should care.

Problem Space
Waiting for disks or data over a network is inherently slow and we have all learned (or been told) by now that writing software that blocks is bad, right? For client side programs, doing I/O or blocking on the UI thread is a great way to create a poor user experience as the app glitches or appears to hang.  For server side programs, new requests can usually just create a new thread if all others are blocked, but that can cause inefficient resource usage as threads are often not a cheap resource. 

However, it is still remarkably difficult to write code that is efficient and truly asynchronous. Different platforms provide different mechanisms and APIs for doing asynchronous I/O. Many APIs don’t have any asynchronous equivalent at all. Often, the solution is to make the call from a worker thread, which calls a blocking API, and then return the result back to the main thread. This can be difficult as well and requires using synchronization mechanisms to avoid concurrency problems. There are libraries that provide abstractions over these disparate mechanisms, however. Examples of this include Boost ASIO, the C++ Rest SDK, and libuv. Boost ASIO and the Rest SDK are C++ libraries and libuv is a C library. They have some overlap between them but each has its own strengths as well.

Libuv is a C library that provides the asynchronous I/O in Node.js. While it was explicitly designed for use by Node.js, it can be used on its own and provides a common cross-platform API, abstracting away the various platform-specific asynchronous APIs. Also, the API exposes a UTF8-only API even on Windows, which is convenient. Every API that can block takes a pointer to a callback function which will be called when the requested operation has completed. An event loop runs and waits for various requests to complete and calls the specified callbacks. For me, writing libuv code was straightforward but it isn’t easy to follow the logic of a program.  Using C++ lambdas for the callback functions can help somewhat, but passing data along the chain of callbacks requires a lot of boilerplate code. For more information on libuv, there is plenty of information on their website. 

There has been a lot of interest in coroutines lately. Many languages have added support for them, and there have been several coroutine proposals submitted to the C++ committee. None have been approved as of yet, but there will likely be coroutine support at some point. One of the coroutine proposals for C++ standardization is resumable functions and the current version of that proposal is N4402, although there are some newer changes as well. It proposes new language syntax for stackless coroutines, and does not define an actual implementation but instead specifies how the language syntax binds to a library implementation. This allows a lot of flexibility and allows supporting different runtime mechanisms.  

Adapting libuv to resumable functions
When I started looking at this, I had never used libuv before, so I initially just wrote some code using straight libuv calls and started thinking about how I would like to be able to write the code. With resumable functions, you can write code that looks very sequential but executes asynchronously.  Whenever the co_await keyword is encountered in a resumable function, the function will “return” if the result of the await expression is not available. 

I had several goals in creating this library. 

Performance should be very good. 
Avoid creating a thick C++ wrapper library. 
Provide a model that should feel familiar to existing libuv users. 
Allow mixing of straight libuv calls with resumable functions.
All of the code I show here and the actual library code as well as a couple of samples is available on github and can be compiled using Visual Studio 2015, Visual Studio 2017, or in this branch of Clang and LLVM that implements this proposal. You will also need CMake and libuv installed. I used version 1.8 of libuv on Linux and 1.10.1 on Windows. If you want to use Clang/LLVM, follow these standard instructions to build it.

I experimented with several different ways to bind libuv to resumable functions, and I show two of these in my library.  The first (and the one I use in the following examples) uses something similar to std::promise and std::future. There is awaituv::promise_t and awaituv::future_t, which point to a shared state object that holds the “return value” from the libuv call. I put “return value” in quotes because the value is provided asynchronously through a callback in libuv. This mechanism requires a heap allocation to hold the shared state. The second mechanism lets the developer put the shared state on the stack of the calling function, which avoids a separate heap allocation and associated shared_ptr machinery. It isn’t as transparent as the first mechanism, but it can be useful for performance. 

Examples
Let’s look at a simple example that writes out “hello world” 1000 times asynchronously.

<pre><code lang=”cpp”> 
future_t<void> start_hello_world()
{
  for (int i = 0; i < 1000; ++i) {
    string_buf_t buf("\nhello world\n");
    fs_t         req;
    (void)co_await fs_write(uv_default_loop(), &req, 1 /*stdout*/, &buf, 1,
                            -1);
  }
} </code></pre>

 A function that uses co_await must have a return type that is an awaitable type, so this function returns a future_t<void>, which implements the methods necessary for the compiler to generate code for a resumable function. This function will loop one thousand times and asynchronously write out “hello world”. The “fs_write” function is in the awaituv namespace and is a thin wrapper over libuv’s uv_fs_write.  Its return type is future_t<int>, which is awaitable. In this case, I am ignoring the actual value but still awaiting the completion. The start_hello_world function “returns” if the result of the await expression is not immediately available, and a pointer to resume the function is stored such that when the write completes the function is resumed. The string_buf_t type is a thin wrapper over the uv_buf_t type, although the raw uv_buf_t type could be used as well. The fs_t type is also a thin wrapper over uv_fs_t and has a destructor that calls uv_fs_cleanup.  This is also not required to be used but does make the code a little cleaner. 

Note: unlike std::future, future_t does not provide a “get” method as that would need to actually block. In the case of libuv, this would essentially hang the program as no callbacks can run unless the event loop is processing. For this to work, you can only await on a future. 

Now let’s look at a slightly more complicated example which reads a file and dumps it to stdout. 

<pre><code lang=”cpp”> 
future_t<void> start_dump_file(const std::string& str)
{ // We can use the same request object for all file operations as they don’t
  // overlap. 
  static_buf_t<1024> buffer;

  fs_t    openreq;
  uv_file file =
      co_await fs_open(uv_default_loop(), &openreq, str.c_str(), O_RDONLY, 0);
  if (file > 0) {
    while (1) {
      fs_t readreq;
      int  result =
          co_await fs_read(uv_default_loop(), &readreq, file, &buffer, 1, -1);
      if (result <= 0)
        break;
      buffer.len = result;
      fs_t req;
      (void)co_await fs_write(uv_default_loop(), &req, 1 /*stdout*/, &buffer,
                              1, -1);
    }
    fs_t closereq;
    (void)co_await fs_close(uv_default_loop(), &closereq, file);
  }
}
</code></pre>

This function should be pretty easy to understand as it is written very much like a synchronous version would be written. The static_buf_t type is another simple C++ wrapper over uv_buf_t that provides a fixed size buffer. This function opens a file, reads a chunk into a buffer, writes it to stdout, iterates until no more data, and then closes the file.  In this case, you can see we are using the result of the await expression when opening the file and when reading data.  Next, let’s look at a function that will change the text color of stdout on a timer. 

<pre><code lang=”cpp”> 
bool           run_timer = true;
uv_timer_t     color_timer;
future_t<void> start_color_changer()
{
  static string_buf_t normal = "\033[40;37m";
  static string_buf_t red = "\033[41;37m";

  uv_timer_init(uv_default_loop(), &color_timer);

  uv_write_t writereq;
  uv_tty_t   tty;
  uv_tty_init(uv_default_loop(), &tty, 1, 0);
  uv_tty_set_mode(&tty, UV_TTY_MODE_NORMAL);

  int cnt = 0;
  unref(&color_timer);

  auto timer = timer_start(&color_timer, 1, 1);

  while (run_timer) {
    (void)co_await timer.next_future();

    if (++cnt % 2 == 0)
      (void)co_await write(&writereq, reinterpret_cast<uv_stream_t*>(&tty),
                           &normal, 1);
    else
      (void)co_await write(&writereq, reinterpret_cast<uv_stream_t*>(&tty),
                           &red, 1);
  }

  // reset back to normal (void) co_await write(&writereq,
  // reinterpret_cast<uv_stream_t*>(&tty), &normal, 1);

  uv_tty_reset_mode();
  co_await close(&tty);
  co_await close(&color_timer); // close handle } </code></pre>

Much of this function is straightforward libuv code, which includes support for processing ANSI escape sequences to set colors. The new concept in this function is that a timer can be recurring and doesn’t have a single completion. The timer_start function (wraps uv_timer_start) returns a promise_t rather than a future_t. To get an awaitable object, you must call “next_future” on the timer. This resets the internal state such that it can be awaited on again. The color_timer variable is a global so that the stop_color_changer function (not shown) can stop the timer. 

Finally, here is a function that opens a socket and sends an http request to google.com. 

<pre><code lang=”cpp”> 
future_t<void> start_http_google()
{
  uv_tcp_t socket;
  if (uv_tcp_init(uv_default_loop(), &socket) ==
      0) { // Use HTTP/1.0 rather than 1.1 so that socket is closed by server
           // when done sending data. // Makes it easier than figuring it out
           // on our end… const char* httpget = "GET / HTTP/1.0\r\n" "Host:
           // www.google.com\r\n" "Cache-Control: max-age=0\r\n" "Accept:
           // text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"
           // "\r\n"; const char* host = "www.google.com";

    uv_getaddrinfo_t req;
    addrinfo_state   addrstate;
    if (co_await getaddrinfo(addrstate, uv_default_loop(), &req, host, "http",
                             nullptr) == 0) {
      uv_connect_t         connectreq;
      awaitable_state<int> connectstate;
      if (co_await tcp_connect(connectstate, &connectreq, &socket,
                               addrstate._addrinfo->ai_addr) == 0) {
        string_buf_t         buffer{ httpget };
        ::uv_write_t         writereq;
        awaitable_state<int> writestate;
        if (co_await write(writestate, &writereq, connectreq.handle, &buffer,
                           1) == 0) {
          read_request_t reader;
          if (read_start(connectreq.handle, &reader) == 0) {
            while (1) {
              auto state = co_await reader.read_next();
              if (state->_nread <= 0)
                break;
              uv_buf_t buf = uv_buf_init(state->_buf.base, state->_nread);
              fs_t     writereq;
              awaitable_state<int> writestate;
              (void)co_await fs_write(writestate, uv_default_loop(), &writereq,
                                      1 /*stdout*/, &buf, 1, -1);
            }
          }
        }
      }
    }
    awaitable_state<void> closestate;
    co_await close(closestate, &socket);
  }
}
 </code></pre>

Again, a couple of new concepts show up in this example.  First, we don’t directly await on getaddrinfo. The getaddrinfo function returns a future_t<addrinfo_state>, which contains two pieces of information. The result of awaiting on future_t<addrinfo_state> gives an integer which indicates success or failure, but there is also a addrinfo pointer, which is used in the tcp_connect call. Finally, reading data on a socket potentially results in multiple callbacks as data arrives.  This requires a different mechanism than just await’ing the read. For this, there is the read_request_t type. As data arrives on a socket, it will pass the data on if there is an outstanding await.  Otherwise, it holds onto that data until the next time an await occurs on it. 

Finally, let’s look at using these functions in combination. 

<pre><code lang=”cpp”> 
int main(int argc, char* argv[])
{ // Process command line if (argc == 1) { printf("testuv [–sequential] <file1>
  // <file2> …"); return -1; }

  bool           fRunSequentially = false;
  vector<string> files;
  for (int i = 1; i < argc; ++i) {
    string str = argv[i];
    if (str == "–sequential")
      fRunSequentially = true;
    else
      files.push_back(str);
  }

  // start async color changer start_color_changer();

  start_hello_world();
  if (fRunSequentially)
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  for (auto& file : files) {
    start_dump_file(file.c_str());
    if (fRunSequentially)
      uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  }

  start_http_google();
  if (fRunSequentially)
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  if (!fRunSequentially)
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  // stop the color changer and let it get cleaned up stop_color_changer();
  // uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  uv_loop_close(uv_default_loop());

  return 0;
}
 </code></pre>

 This function supports two modes: the default parallel mode and a sequential mode. In sequential mode, we will run the libuv event loop after each task is started, allowing it to complete before starting the next. In parallel mode, all tasks (resumabled functions) are started and then resumed as awaits are completed.

Implementation
This library is currently header only. Let’s look at one of the wrapper functions. 

<pre><code lang=”cpp”> 
auto fs_open(uv_loop_t*  loop,
             uv_fs_t*    req,
             const char* path,
             int         flags,
             int         mode)
{
  promise_t<uv_file> awaitable;
  auto               state = awaitable._state->lock();
  req->data = state;

  auto ret =
      uv_fs_open(loop, req, path, flags, mode, [](uv_fs_t* req) -> void {
        auto state = static_cast<promise_t<uv_file>::state_type*>(req->data);
        state->set_value(req->result);
        state->unlock();
      });

  if (ret != 0) {
    state->set_value(ret);
    state->unlock();
  }
  return awaitable.get_future();
}
 </code></pre>

This function wraps the uv_fs_open function and the signature is almost identical to it.  It doesn’t take a callback and it returns future<int> rather than int. Internally, the promise_t<int> holds a reference counted state object, which contains an int and some other housekeeping information. Libuv provides a “data” member to hold implementation specific information, which for us is a raw pointer to the state object. The actual callback passed to the uv_fs_open function is a lambda which will cast “data” back to a state object and call its set_value method. If uv_fs_open returned a failure (which means the callback will never be invoked), we directly set the value of the promise. Finally, we return a future that also has a reference counted pointer to the state. The returned future implements the necessary methods for co_await to work with it. 

I currently have wrappers for the following libuv functions:

uv_ref/uv_unref
uv_fs_open
uv_fs_close
uv_fs_read
uv_fs_write
uv_write
uv_close
uv_timer_start
uv_tcp_connect
uv_getaddrinfo
uv_read_start
This library is far from complete and wrappers for other libuv functions need to be completed. I have also not explored cancellation or propagation of errors. I believe there is a better way to handle the multiple callbacks of uv_read_start and uv_timer_start, but I haven’t found something I’m completely happy with. Perhaps it should remain callback-based given its recurrency.

Summary
For me, coroutines provide a simpler to follow model for asynchronous programming with libuv. Download the library and samples from the Github repo. Let me know what you think of this approach and how useful it would be

source: https://devblogs.microsoft.com/cppblog/using-ibuv-with-c-resumable-functions/