// TestUV.cpp : Defines the entry point for the console application.
//

#include <awaituv.h>
#include <fcntl.h>
#include <string>
#include <vector>

using namespace awaituv;
using namespace std;

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
  // unref the timer so that its existence won't keep
  // the loop alive
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

  // reset back to normal
  (void)co_await write(&writereq, reinterpret_cast<uv_stream_t*>(&tty),
                       &normal, 1);

  uv_tty_reset_mode();
  co_await close(&tty);
  co_await close(&color_timer); // close handle
}

void stop_color_changer()
{
  run_timer = false;
  // re-ref it so that loop won't exit until function above is done.
  ref(&color_timer);
}

future_t<void> start_http_google()
{
  uv_tcp_t socket;
  if (uv_tcp_init(uv_default_loop(), &socket) == 0) {
    // Use HTTP/1.0 rather than 1.1 so that socket is closed by server when
    // done sending data. Makes it easier than figuring it out on our end...
    const char* httpget =
        "GET / HTTP/1.0\r\n"
        "Host: www.baidu.com\r\n"
        "Cache-Control: max-age=0\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"
        "\r\n";
    const char* host = "www.baidu.com";

    uv_getaddrinfo_t req;
    auto addr = getaddrinfo(uv_default_loop(), &req, host, "http", nullptr);
    if (co_await addr == 0) {
      uv_connect_t connectreq;
      auto         connect =
          tcp_connect(&connectreq, &socket, addr._state->_addrinfo->ai_addr);
      if (co_await connect == 0) {
        string_buf_t buffer{ httpget };
        ::uv_write_t writereq;
        if (co_await write(&writereq, connectreq.handle, &buffer, 1) == 0) {
          read_request_t reader;
          if (read_start(connectreq.handle, &reader) == 0) {
            while (1) {
              auto state = co_await reader.read_next();
              if (state->_nread <= 0)
                break;
              uv_buf_t buf = uv_buf_init(state->_buf.base, state->_nread);
              fs_t     writereq;
              (void)co_await fs_write(uv_default_loop(), &writereq,
                                      1 /*stdout*/, &buf, 1, -1);
            }
          }
        }
      }
    }
    co_await close(&socket);
  }
}

/*future_t<void> start_dump_file(const std::string& str)
{
  // We can use the same request object for all file operations as they don't
  // overlap.
  static_buf_t<1024> buffer;

  fs_t         openreq;
  uv_file      file =
      co_await fs_open(uv_default_loop(), &openreq, str.c_str(), O_RDONLY, 0);
  if (file > 0) {
    while (1) {
      fs_t         readreq;
      int          result =
          co_await fs_read(uv_default_loop(), &readreq, file, &buffer, 1, -1);
      if (result <= 0)
        break;
      buffer.len = result;
      fs_t           req;
      (void)co_await fs_write(uv_default_loop(), &req, 1 , &buffer,
                              1, -1);
    }
    fs_t           closereq;
    (void)co_await fs_close(uv_default_loop(), &closereq, file);
  }
}
*/
future_t<void> start_hello_world()
{
  for (int i = 0; i < 1000; ++i) {
    string_buf_t buf("\nhello world\n");
    fs_t         req;
    (void)co_await fs_write(uv_default_loop(), &req, 1 /*stdout*/, &buf, 1,
                            -1);
  }
}

int main(int argc, char* argv[])
{
  // Process command line
  if (argc == 1) {
    printf("testuv [--sequential] <file1> <file2> ...");
    return -1;
  }

  bool           fRunSequentially = false;
  vector<string> files;
  for (int i = 1; i < argc; ++i) {
    string str = argv[i];
    if (str == "--sequential")
      fRunSequentially = true;
    else
      files.push_back(str);
  }
  

  // start async color changer
  start_color_changer();

  // start_hello_world();
  if (fRunSequentially)
     uv_run(uv_default_loop(), UV_RUN_DEFAULT);


  start_http_google();
  if (fRunSequentially)
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  if (!fRunSequentially)
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  // stop the color changer and let it get cleaned up

  stop_color_changer();
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  uv_loop_close(uv_default_loop());

  return 0;
}
