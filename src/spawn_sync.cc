// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "node.h"
#include "node_buffer.h"
#include "env-inl.h"
#include "string_bytes.h"

#include <string.h>
#include <stdlib.h>


namespace node {

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Null;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;


class SyncProcessOutputBuffer;
class SyncProcessStdioPipe;
class SyncProcessRunner;


class SyncProcessOutputBuffer {
  static const unsigned int kBufferSize = 65536;

 public:
  inline SyncProcessOutputBuffer();

  inline void OnAlloc(size_t suggested_size, uv_buf_t* buf) const;
  inline void OnRead(const uv_buf_t* buf, size_t nread);

  inline size_t Copy(char* dest) const;

  inline unsigned int available() const;
  inline unsigned int used() const;

  inline SyncProcessOutputBuffer* next() const;
  inline void set_next(SyncProcessOutputBuffer* next);

 private:
  // Use unsigned int because that's what `uv_buf_init` takes.
  mutable char data_[kBufferSize];
  unsigned int used_;

  SyncProcessOutputBuffer* next_;
};


class SyncProcessStdioPipe {
 enum Lifecycle {
   kUninitialized = 0,
   kInitialized,
   kStarted,
   kClosing,
   kClosed
 };

 public:
  SyncProcessStdioPipe(SyncProcessRunner* process_handler,
                       bool readable,
                       bool writable,
                       uv_buf_t input_buffer);
  ~SyncProcessStdioPipe();

  int Initialize(uv_loop_t* loop);
  int Start();
  void Close();

  Local<Object> GetOutputAsBuffer() const;

  inline bool readable() const;
  inline bool writable() const;
  inline uv_stdio_flags uv_flags() const;

  inline uv_pipe_t* uv_pipe() const;
  inline uv_stream_t* uv_stream() const;
  inline uv_handle_t* uv_handle() const;

 private:
  inline size_t OutputLength() const;
  inline void CopyOutput(char* dest) const;

  inline void OnAlloc(size_t suggested_size, uv_buf_t* buf);
  inline void OnRead(const uv_buf_t* buf, ssize_t nread);
  inline void OnWriteDone(int result);
  inline void OnShutdownDone(int result);
  inline void OnClose();

  inline void SetError(int error);

  static void AllocCallback(uv_handle_t* handle,
                            size_t suggested_size,
                            uv_buf_t* buf);
  static void ReadCallback(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf);
  static void WriteCallback(uv_write_t* req, int result);
  static void ShutdownCallback(uv_shutdown_t* req, int result);
  static void CloseCallback(uv_handle_t* handle);

  SyncProcessRunner* process_handler_;

  bool readable_;
  bool writable_;
  uv_buf_t input_buffer_;

  SyncProcessOutputBuffer* first_output_buffer_;
  SyncProcessOutputBuffer* last_output_buffer_;

  mutable uv_pipe_t uv_pipe_;
  uv_write_t write_req_;
  uv_shutdown_t shutdown_req_;

  Lifecycle lifecycle_;
};


class SyncProcessRunner {
  enum Lifecycle {
    kUninitialized = 0,
    kInitialized,
    kHandlesClosed
  };

 public:
  static void Initialize(Handle<Object> target,
                         Handle<Value> unused,
                         Handle<Context> context);
  static void Spawn(const FunctionCallbackInfo<Value>& args);

 private:
  friend class SyncProcessStdioPipe;

  SyncProcessRunner(Environment* env_);
  ~SyncProcessRunner();

  inline Environment* env() const;

  Local<Object> Run(Local<Value> options);
  void TryInitializeAndRunLoop(Local<Value> options);
  void CloseHandlesAndDeleteLoop();

  void CloseStdioPipes();
  void CloseKillTimer();

  void Kill();
  void IncrementBufferSizeAndCheckOverflow(ssize_t length);

  void OnExit(int64_t exit_status, int term_signal);
  void OnKillTimerTimeout(int status);

  int GetError();
  void SetError(int error);
  void SetPipeError(int pipe_error);

  Local<Object> BuildResultObject();
  Local<Array> BuildOutputArray();

  int ParseOptions(Local<Value> js_value);
  int ParseStdioOptions(Local<Value> js_value);
  int ParseStdioOption(int child_fd, Local<Object> js_stdio_option);

  inline int AddStdioIgnore(uint32_t child_fd);
  inline int AddStdioPipe(uint32_t child_fd,
                          bool readable,
                          bool writable,
                          uv_buf_t input_buffer);
  inline int AddStdioInheritFD(uint32_t child_fd, int inherit_fd);

  static bool IsSet(Local<Value> value);
  template <typename t> static bool CheckRange(Local<Value> js_value);
  static int CopyJsString(Local<Value> js_value, const char** target);
  static int CopyJsStringArray(Local<Value> js_value, char** target);

  static void ExitCallback(uv_process_t* handle,
                           int64_t exit_status,
                           int term_signal);
  static void KillTimerCallback(uv_timer_t* handle, int status);
  static void KillTimerCloseCallback(uv_handle_t* handle);

  size_t max_buffer_;
  uint64_t timeout_;
  int kill_signal_;

  uv_loop_t* uv_loop_;

  uint32_t stdio_count_;
  uv_stdio_container_t* uv_stdio_containers_;
  SyncProcessStdioPipe** stdio_pipes_;
  bool stdio_pipes_initialized_;

  uv_process_options_t uv_process_options_;
  const char* file_buffer_;
  char* args_buffer_;
  char* env_buffer_;
  const char* cwd_buffer_;

  uv_process_t uv_process_;
  bool killed_;

  size_t buffered_output_size_;
  int64_t exit_status_;
  int term_signal_;

  uv_timer_t uv_timer_;
  bool kill_timer_initialized_;

  // Errors that happen in one of the pipe handlers are stored in the
  // `pipe_error` field. They are treated as "low-priority", only to be
  // reported if no more serious errors happened.
  int error_;
  int pipe_error_;

  Lifecycle lifecycle_;

  Environment* env_;
};


SyncProcessOutputBuffer::SyncProcessOutputBuffer()
    : used_(0),
      next_(NULL) {
}


void SyncProcessOutputBuffer::OnAlloc(size_t suggested_size,
                                      uv_buf_t* buf) const {
  if (used() == kBufferSize)
    *buf = uv_buf_init(NULL, 0);
  else
    *buf = uv_buf_init(data_ + used(), available());
}


void SyncProcessOutputBuffer::OnRead(const uv_buf_t* buf, size_t nread) {
  // If we hand out the same chunk twice, this should catch it.
  assert(buf->base == data_ + used());
  used_ += static_cast<unsigned int>(nread);
}


size_t SyncProcessOutputBuffer::Copy(char* dest) const {
  memcpy(dest, data_, used());
  return used();
}


unsigned int SyncProcessOutputBuffer::available() const {
  return sizeof data_ - used();
}


unsigned int SyncProcessOutputBuffer::used() const {
  return used_;
}


SyncProcessOutputBuffer* SyncProcessOutputBuffer::next() const {
  return next_;
}


void SyncProcessOutputBuffer::set_next(SyncProcessOutputBuffer* next) {
  next_ = next;
}


SyncProcessStdioPipe::SyncProcessStdioPipe(SyncProcessRunner* process_handler,
                                           bool readable,
                                           bool writable,
                                           uv_buf_t input_buffer)
    : process_handler_(process_handler),
      readable_(readable),
      writable_(writable),
      input_buffer_(input_buffer),

      first_output_buffer_(NULL),
      last_output_buffer_(NULL),

      uv_pipe_(),
      write_req_(),
      shutdown_req_(),

      lifecycle_(kUninitialized) {
  assert(readable || writable);
}


SyncProcessStdioPipe::~SyncProcessStdioPipe() {
  assert(lifecycle_ == kUninitialized || lifecycle_ == kClosed);

  SyncProcessOutputBuffer* buf;
  SyncProcessOutputBuffer* next;

  for (buf = first_output_buffer_; buf != NULL; buf = next) {
    next = buf->next();
    delete buf;
  }
}


int SyncProcessStdioPipe::Initialize(uv_loop_t* loop) {
  assert(lifecycle_ == kUninitialized);

  int r = uv_pipe_init(loop, uv_pipe(), 0);
  if (r < 0)
    return r;

  uv_pipe()->data = this;

  lifecycle_ = kInitialized;
  return 0;
}


int SyncProcessStdioPipe::Start() {
  assert(lifecycle_ == kInitialized);

  // Set the busy flag already. If this function fails no recovery is
  // possible.
  lifecycle_ = kStarted;

  if (readable()) {
    if (input_buffer_.len > 0) {
      assert(input_buffer_.base != NULL);

      int r = uv_write(&write_req_,
                       uv_stream(),
                       &input_buffer_,
                       1,
                       WriteCallback);
      if (r < 0)
        return r;
    }

    int r = uv_shutdown(&shutdown_req_, uv_stream(), ShutdownCallback);
    if (r < 0)
      return r;
  }

  if (writable()) {
    int r = uv_read_start(uv_stream(), AllocCallback, ReadCallback);
    if (r < 0)
      return r;
  }

  return 0;
}


void SyncProcessStdioPipe::Close() {
  assert(lifecycle_ == kInitialized || lifecycle_ == kStarted);

  uv_close(uv_handle(), CloseCallback);

  lifecycle_ = kClosing;
}


Local<Object> SyncProcessStdioPipe::GetOutputAsBuffer() const {
  size_t length = OutputLength();
  Local<Object> js_buffer = Buffer::New(length);
  CopyOutput(Buffer::Data(js_buffer));
  return js_buffer;
}


bool SyncProcessStdioPipe::readable() const {
  return readable_;
}


bool SyncProcessStdioPipe::writable() const {
  return writable_;
}


uv_stdio_flags SyncProcessStdioPipe::uv_flags() const {
  unsigned int flags;

  flags = UV_CREATE_PIPE;
  if (readable())
    flags |= UV_READABLE_PIPE;
  if (writable())
    flags |= UV_WRITABLE_PIPE;

  return static_cast<uv_stdio_flags>(flags);
}


uv_pipe_t* SyncProcessStdioPipe::uv_pipe() const {
  assert(lifecycle_ < kClosing);
  return &uv_pipe_;
}


uv_stream_t* SyncProcessStdioPipe::uv_stream() const {
  return reinterpret_cast<uv_stream_t*>(uv_pipe());
}


uv_handle_t* SyncProcessStdioPipe::uv_handle() const {
  return reinterpret_cast<uv_handle_t*>(uv_pipe());
}


size_t SyncProcessStdioPipe::OutputLength() const {
  SyncProcessOutputBuffer* buf;
  size_t size = 0;

  for (buf = first_output_buffer_; buf != NULL; buf = buf->next())
    size += buf->used();

  return size;
}


void SyncProcessStdioPipe::CopyOutput(char* dest) const {
  SyncProcessOutputBuffer* buf;
  size_t offset = 0;

  for (buf = first_output_buffer_; buf != NULL; buf = buf->next())
    offset += buf->Copy(dest + offset);
}


void SyncProcessStdioPipe::OnAlloc(size_t suggested_size, uv_buf_t* buf) {
  // This function assumes that libuv will never allocate two buffers for the
  // same stream at the same time. There's an assert in
  // SyncProcessOutputBuffer::OnRead that would fail if this assumption was
  // ever violated.

  if (last_output_buffer_ == NULL) {
    // Allocate the first capture buffer.
    first_output_buffer_ = new SyncProcessOutputBuffer();
    last_output_buffer_ = first_output_buffer_;

  } else if (last_output_buffer_->available() == 0) {
    // The current capture buffer is full so get us a new one.
    SyncProcessOutputBuffer* buf = new SyncProcessOutputBuffer();
    last_output_buffer_->set_next(buf);
    last_output_buffer_ = buf;
  }

  last_output_buffer_->OnAlloc(suggested_size, buf);
}


void SyncProcessStdioPipe::OnRead(const uv_buf_t* buf, ssize_t nread) {
  if (nread == UV_EOF) {
    // Libuv implicitly stops reading on EOF.

  } else if (nread < 0) {
    SetError(static_cast<int>(nread));
    // At some point libuv should really implicitly stop reading on error.
    uv_read_stop(uv_stream());

  } else {
    last_output_buffer_->OnRead(buf, nread);
    process_handler_->IncrementBufferSizeAndCheckOverflow(nread);
  }
}


void SyncProcessStdioPipe::OnWriteDone(int result) {
  if (result < 0)
    SetError(result);
}


void SyncProcessStdioPipe::OnShutdownDone(int result) {
  if (result < 0)
    SetError(result);
}


void SyncProcessStdioPipe::OnClose() {
  lifecycle_ = kClosed;
}


void SyncProcessStdioPipe::SetError(int error) {
  assert(error != 0);
  process_handler_->SetPipeError(error);
}


void SyncProcessStdioPipe::AllocCallback(uv_handle_t* handle,
                                         size_t suggested_size,
                                         uv_buf_t* buf) {
  SyncProcessStdioPipe* self =
      reinterpret_cast<SyncProcessStdioPipe*>(handle->data);
  self->OnAlloc(suggested_size, buf);
}


void SyncProcessStdioPipe::ReadCallback(uv_stream_t* stream,
                                        ssize_t nread,
                                        const uv_buf_t* buf) {
  SyncProcessStdioPipe* self =
        reinterpret_cast<SyncProcessStdioPipe*>(stream->data);
  self->OnRead(buf, nread);
}


void SyncProcessStdioPipe::WriteCallback(uv_write_t* req, int result) {
  SyncProcessStdioPipe* self =
      reinterpret_cast<SyncProcessStdioPipe*>(req->handle->data);
  self->OnWriteDone(result);
}


void SyncProcessStdioPipe::ShutdownCallback(uv_shutdown_t* req, int result) {
  SyncProcessStdioPipe* self =
      reinterpret_cast<SyncProcessStdioPipe*>(req->handle->data);
  self->OnShutdownDone(result);
}


void SyncProcessStdioPipe::CloseCallback(uv_handle_t* handle) {
  SyncProcessStdioPipe* self =
      reinterpret_cast<SyncProcessStdioPipe*>(handle->data);
  self->OnClose();
}


void SyncProcessRunner::Initialize(Handle<Object> target,
                                   Handle<Value> unused,
                                   Handle<Context> context) {
  Environment* env = Environment::GetCurrent(context);
  NODE_SET_METHOD(target, "spawn", Spawn);
}


void SyncProcessRunner::Spawn(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  HandleScope scope(isolate);
  SyncProcessRunner p(Environment::GetCurrent(isolate));
  Local<Value> result = p.Run(args[0]);
  args.GetReturnValue().Set(result);
}


SyncProcessRunner::SyncProcessRunner(Environment* env)
    : max_buffer_(0),
      timeout_(0),
      kill_signal_(SIGTERM),

      uv_loop_(NULL),

      stdio_count_(0),
      uv_stdio_containers_(NULL),
      stdio_pipes_(NULL),
      stdio_pipes_initialized_(false),

      uv_process_options_(),
      file_buffer_(NULL),
      args_buffer_(NULL),
      env_buffer_(NULL),
      cwd_buffer_(NULL),

      uv_process_(),
      killed_(false),

      buffered_output_size_(0),
      exit_status_(-1),
      term_signal_(-1),

      uv_timer_(),
      kill_timer_initialized_(false),

      error_(0),
      pipe_error_(0),

      lifecycle_(kUninitialized),

      env_(env)
{
}


SyncProcessRunner::~SyncProcessRunner() {
  assert(lifecycle_ == kHandlesClosed);

  if (stdio_pipes_ != NULL) {
    for (size_t i = 0; i < stdio_count_; i++) {
      if (stdio_pipes_[i] != NULL)
        delete stdio_pipes_[i];
    }
  }

  delete[] stdio_pipes_;
  delete[] file_buffer_;
  delete[] args_buffer_;
  delete[] cwd_buffer_;
  delete[] env_buffer_;
  delete[] uv_stdio_containers_;
}


Environment* SyncProcessRunner::env() const {
  return env_;
}


Local<Object> SyncProcessRunner::Run(Local<Value> options) {
  HandleScope scope(node_isolate);

  assert(lifecycle_ == kUninitialized);

  TryInitializeAndRunLoop(options);
  CloseHandlesAndDeleteLoop();

  Local<Object> result = BuildResultObject();

  return scope.Close(result);
}


void SyncProcessRunner::TryInitializeAndRunLoop(Local<Value> options) {
  int r;

  // There is no recovery from failure inside TryInitializeAndRunLoop - the
  // only option we'd have is to close all handles and destroy the loop.
  assert(lifecycle_ == kUninitialized);
  lifecycle_ = kInitialized;

  uv_loop_ = uv_loop_new();
  if (uv_loop_ == NULL)
    return SetError(UV_ENOMEM);

  r = ParseOptions(options);
  if (r < 0)
    return SetError(r);

  if (timeout_ > 0) {
    r = uv_timer_init(uv_loop_, &uv_timer_);
    if (r < 0)
      return SetError(r);

    uv_unref(reinterpret_cast<uv_handle_t*>(&uv_timer_));

    uv_timer_.data = this;
    kill_timer_initialized_ = true;

    // Start the timer immediately. If uv_spawn fails then
    // CloseHandlesAndDeleteLoop() will immediately close the timer handle
    // which implicitly stops it, so there is no risk that the timeout callback
    // runs when the process didn't start.
    r = uv_timer_start(&uv_timer_, KillTimerCallback, timeout_, 0);
    if (r < 0)
      return SetError(r);
  }

  uv_process_options_.exit_cb = ExitCallback;
  r = uv_spawn(uv_loop_, &uv_process_, &uv_process_options_);
  if (r < 0)
    return SetError(r);
  uv_process_.data = this;

  for (uint32_t i = 0; i < stdio_count_; i++) {
    SyncProcessStdioPipe* h = stdio_pipes_[i];
    if (h != NULL) {
      r = h->Start();
      if (r < 0)
        return SetPipeError(r);
    }
  }

  r = uv_run(uv_loop_, UV_RUN_DEFAULT);
  if (r < 0)
    // We can't handle uv_run failure.
    abort();

  // If we get here the process should have exited.
  assert(exit_status_ >= 0);
}


void SyncProcessRunner::CloseHandlesAndDeleteLoop() {
  assert(lifecycle_ < kHandlesClosed);

  if (uv_loop_ != NULL) {
    CloseStdioPipes();
    CloseKillTimer();

    // Give closing watchers a chance to finish closing and get their close
    // callbacks called.
    int r = uv_run(uv_loop_, UV_RUN_DEFAULT);
    if (r < 0)
      abort();

    uv_loop_delete(uv_loop_);

  } else {
    // If the loop doesn't exist, neither should any pipes or timers.
    assert(!stdio_pipes_initialized_);
    assert(!kill_timer_initialized_);
  }

  lifecycle_ = kHandlesClosed;
}


void SyncProcessRunner::CloseStdioPipes() {
  assert(lifecycle_ < kHandlesClosed);

  if (stdio_pipes_initialized_) {
    assert(stdio_pipes_ != NULL);
    assert(uv_loop_ != NULL);

    for (uint32_t i = 0; i < stdio_count_; i++) {
      if (stdio_pipes_[i] != NULL)
        stdio_pipes_[i]->Close();
    }

    stdio_pipes_initialized_ = false;
  }
}


void SyncProcessRunner::CloseKillTimer() {
  assert(lifecycle_ < kHandlesClosed);

  if (kill_timer_initialized_) {
    assert(timeout_ > 0);
    assert(uv_loop_ != NULL);

    uv_handle_t* uv_timer_handle = reinterpret_cast<uv_handle_t*>(&uv_timer_);
    uv_ref(uv_timer_handle);
    uv_close(uv_timer_handle, KillTimerCloseCallback);

    kill_timer_initialized_ = false;
  }
}


void SyncProcessRunner::Kill() {
  // Only attempt to kill once.
  if (killed_)
    return;
  killed_ = true;

  // We might get here even if the process we spawned has already exited. This
  // could happen when our child process spawned another process which
  // inherited (one of) the stdio pipes. In this case we won't attempt to send
  // a signal to the process, however we will still close our end of the stdio
  // pipes so this situation won't make us hang.
  if (exit_status_ < 0) {
    int r = uv_process_kill(&uv_process_, kill_signal_);

    // If uv_kill failed with an error that isn't ESRCH, the user probably
    // specified an invalid or unsupported signal. Signal this to the user as
    // and error and kill the process with SIGKILL instead.
    if (r < 0 && r != UV_ESRCH) {
      SetError(r);

      r = uv_process_kill(&uv_process_, SIGKILL);
      assert(r >= 0 || r == UV_ESRCH);
    }
  }

  // Close all stdio pipes.
  CloseStdioPipes();

  // Stop the timeout timer immediately.
  CloseKillTimer();
}


void SyncProcessRunner::IncrementBufferSizeAndCheckOverflow(ssize_t length) {
  buffered_output_size_ += length;

  if (max_buffer_ > 0 && buffered_output_size_ > max_buffer_) {
    SetError(UV_ENOBUFS);
    Kill();
  }
}


void SyncProcessRunner::OnExit(int64_t exit_status, int term_signal) {
  if (exit_status < 0)
    return SetError(static_cast<int>(exit_status));

  exit_status_ = exit_status;
  term_signal_ = term_signal;
}


void SyncProcessRunner::OnKillTimerTimeout(int status) {
  assert(status == 0);
  SetError(UV_ETIMEDOUT);
  Kill();
}


int SyncProcessRunner::GetError() {
  if (error_ != 0)
    return error_;
  else
    return pipe_error_;
}


void SyncProcessRunner::SetError(int error) {
  if (error_ == 0)
    error_ = error;
}


void SyncProcessRunner::SetPipeError(int pipe_error) {
  if (pipe_error_ == 0)
    pipe_error_ = pipe_error;
}


Local<Object> SyncProcessRunner::BuildResultObject() {
  HandleScope scope(node_isolate);

  Local<Object> js_result = Object::New();

  if (GetError() != 0)
    js_result->Set(env()->error_sym(), Integer::New(GetError()));

  if (exit_status_ >= 0)
    js_result->Set(env()->status_sym(),
        Number::New(node_isolate, static_cast<double>(exit_status_)));
  else
    // If exit_status_ < 0 the process was never started because of some error.
    js_result->Set(env()->status_sym(), Null(node_isolate));

  if (term_signal_ > 0)
    js_result->Set(env()->signal_sym(),
        String::NewFromUtf8(node_isolate, signo_string(term_signal_)));
  else
    js_result->Set(env()->signal_sym(), Null());

  if (exit_status_ >= 0)
    js_result->Set(env()->output_sym(), BuildOutputArray());
  else
    js_result->Set(env()->output_sym(), Null(node_isolate));

  return scope.Close(js_result);
}


Local<Array> SyncProcessRunner::BuildOutputArray() {
  assert(lifecycle_ >= kInitialized);
  assert(stdio_pipes_ != NULL);

  HandleScope scope(node_isolate);
  Local<Array> js_output = Array::New(stdio_count_);

  for (uint32_t i = 0; i < stdio_count_; i++) {
    SyncProcessStdioPipe* h = stdio_pipes_[i];
    if (h != NULL && h->writable())
      js_output->Set(i, h->GetOutputAsBuffer());
    else
      js_output->Set(i, Null());
  }

  return scope.Close(js_output);
}


int SyncProcessRunner::ParseOptions(Local<Value> js_value) {
  HandleScope scope(node_isolate);
  int r;

  if (!js_value->IsObject())
    return UV_EINVAL;

  Local<Object> js_options = js_value.As<Object>();

  Local<Value> js_file = js_options->Get(env()->file_sym());
  r = CopyJsString(js_file, &file_buffer_);
  if (r < 0)
    return r;
  uv_process_options_.file = file_buffer_;

  Local<Value> js_args = js_options->Get(env()->args_sym());
  r = CopyJsStringArray(js_args, &args_buffer_);
  if (r < 0)
    return r;
  uv_process_options_.args = reinterpret_cast<char**>(args_buffer_);


  Local<Value> js_cwd = js_options->Get(env()->cwd_sym());
  if (IsSet(js_cwd)) {
    r = CopyJsString(js_cwd, &uv_process_options_.cwd);
    if (r < 0)
      return r;
    uv_process_options_.cwd = cwd_buffer_;
  }

  Local<Value> js_env_pairs = js_options->Get(env()->env_pairs_sym());
  if (IsSet(js_env_pairs)) {
    r = CopyJsStringArray(js_env_pairs, &env_buffer_);
    if (r < 0)
      return r;
    uv_process_options_.args = reinterpret_cast<char**>(env_buffer_);
  }

  Local<Value> js_uid = js_options->Get(env()->uid_sym());
  if (IsSet(js_uid)) {
    if (!CheckRange<uv_uid_t>(js_uid))
      return UV_EINVAL;
    uv_process_options_.uid = static_cast<uv_gid_t>(js_uid->Int32Value());
    uv_process_options_.flags |= UV_PROCESS_SETUID;
  }

  Local<Value> js_gid = js_options->Get(env()->gid_sym());
  if (IsSet(js_gid)) {
    if (!CheckRange<uv_gid_t>(js_gid))
      return UV_EINVAL;
    uv_process_options_.gid = static_cast<uv_gid_t>(js_gid->Int32Value());
    uv_process_options_.flags |= UV_PROCESS_SETGID;
  }

  if (js_options->Get(env()->detached_sym())->BooleanValue())
    uv_process_options_.flags |= UV_PROCESS_DETACHED;

  if (js_options->Get(env()->windows_verbatim_arguments_sym())->BooleanValue())
    uv_process_options_.flags |= UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;

  Local<Value> js_timeout = js_options->Get(env()->timeout_sym());
  if (IsSet(js_timeout)) {
    if (!js_timeout->IsNumber())
      return UV_EINVAL;
    int64_t timeout = js_timeout->IntegerValue();
    if (timeout < 0)
      return UV_EINVAL;
    timeout_ = static_cast<uint64_t>(timeout);
  }

  Local<Value> js_max_buffer = js_options->Get(env()->max_buffer_sym());
  if (IsSet(js_max_buffer)) {
    if (!CheckRange<uint32_t>(js_max_buffer))
      return UV_EINVAL;
    max_buffer_ = js_max_buffer->Uint32Value();
  }

  Local<Value> js_kill_signal = js_options->Get(env()->kill_signal_sym());
  if (IsSet(js_kill_signal)) {
    if (!js_kill_signal->IsInt32())
      return UV_EINVAL;
    kill_signal_ = js_kill_signal->Int32Value();
    if (kill_signal_ == 0)
      return UV_EINVAL;
  }

  Local<Value> js_stdio = js_options->Get(env()->stdio_sym());
  r = ParseStdioOptions(js_stdio);
  if (r < 0)
    return r;

  return 0;
}


int SyncProcessRunner::ParseStdioOptions(Local<Value> js_value) {
  HandleScope scope(node_isolate);
  Local<Array> js_stdio_options;

  if (!js_value->IsArray())
    return UV_EINVAL;

  js_stdio_options = js_value.As<Array>();

  stdio_count_ = js_stdio_options->Length();
  uv_stdio_containers_ = new uv_stdio_container_t[stdio_count_];

  stdio_pipes_ = new SyncProcessStdioPipe*[stdio_count_]();
  stdio_pipes_initialized_ = true;

  for (uint32_t i = 0; i < stdio_count_; i++) {
    Local<Value> js_stdio_option = js_stdio_options->Get(i);

    if (!js_stdio_option->IsObject())
      return UV_EINVAL;

    int r = ParseStdioOption(i, js_stdio_option.As<Object>());
    if (r < 0)
      return r;
  }

  uv_process_options_.stdio = uv_stdio_containers_;
  uv_process_options_.stdio_count = stdio_count_;

  return 0;
}


int SyncProcessRunner::ParseStdioOption(int child_fd,
                                        Local<Object> js_stdio_option) {
  Local<Value> js_type = js_stdio_option->Get(env()->type_sym());

  if (js_type->StrictEquals(env()->ignore_sym())) {
    return AddStdioIgnore(child_fd);

  } else if (js_type->StrictEquals(env()->pipe_sym())) {
    bool readable = js_stdio_option->Get(env()->readable_sym())->BooleanValue();
    bool writable = js_stdio_option->Get(env()->writable_sym())->BooleanValue();

    uv_buf_t buf = uv_buf_init(NULL, 0);

    if (readable) {
      Local<Value> input = js_stdio_option->Get(env()->input_sym());
      if (!Buffer::HasInstance(input))
        // We can only deal with buffers for now.
        assert(input->IsUndefined());
      else
        buf = uv_buf_init(Buffer::Data(input),
                          static_cast<unsigned int>(Buffer::Length(input)));
    }

    return AddStdioPipe(child_fd, readable, writable, buf);

  } else if (js_type->StrictEquals(env()->inherit_sym())) {
    int inherit_fd = js_stdio_option->Get(env()->fd_sym())->Int32Value();
    return AddStdioInheritFD(child_fd, inherit_fd);

  } else {
    assert(0 && "invalid child stdio type");
    return UV_EINVAL;
  }
}


int SyncProcessRunner::AddStdioIgnore(uint32_t child_fd) {
  assert(child_fd < stdio_count_);
  assert(stdio_pipes_[child_fd] == NULL);

  uv_stdio_containers_[child_fd].flags = UV_IGNORE;

  return 0;
}


int SyncProcessRunner::AddStdioPipe(uint32_t child_fd,
                                    bool readable,
                                    bool writable,
                                    uv_buf_t input_buffer) {
  assert(child_fd < stdio_count_);
  assert(stdio_pipes_[child_fd] == NULL);

  SyncProcessStdioPipe* h = new SyncProcessStdioPipe(this,
                                                     readable,
                                                     writable,
                                                     input_buffer);

  int r = h->Initialize(uv_loop_);
  if (r < 0) {
    delete h;
    return r;
  }

  stdio_pipes_[child_fd] = h;

  uv_stdio_containers_[child_fd].flags = h->uv_flags();
  uv_stdio_containers_[child_fd].data.stream = h->uv_stream();

  return 0;
}


int SyncProcessRunner::AddStdioInheritFD(uint32_t child_fd, int inherit_fd) {
  assert(child_fd < stdio_count_);
  assert(stdio_pipes_[child_fd] == NULL);

  uv_stdio_containers_[child_fd].flags = UV_INHERIT_FD;
  uv_stdio_containers_[child_fd].data.fd = inherit_fd;

  return 0;
}


bool SyncProcessRunner::IsSet(Local<Value> value) {
  return !value->IsUndefined() && !value->IsNull();
}


template <typename t>
bool SyncProcessRunner::CheckRange(Local<Value> js_value) {
  if ((t) -1 > 0) {
    // Unsigned range check.
    if (!js_value->IsUint32())
      return false;
    if (js_value->Uint32Value() & ~((t) ~0))
      return false;

  } else {
    // Signed range check.
    if (!js_value->IsInt32())
      return false;
    if (js_value->Int32Value() & ~((t) ~0))
      return false;
  }

  return true;
}


int SyncProcessRunner::CopyJsString(Local<Value> js_value, const char** target) {
  Local<String> js_string;
  size_t size, written;
  char* buffer;

  if (js_value->IsString())
    js_string = js_value.As<String>();
  else
    js_string = js_value->ToString();

  // Include space for null terminator byte.
  size = StringBytes::StorageSize(js_string, UTF8) + 1;

  buffer = new char[size];

  written = StringBytes::Write(buffer, -1, js_string, UTF8);
  buffer[written] = '\0';

  *target = buffer;
  return 0;
}


int SyncProcessRunner::CopyJsStringArray(Local<Value> js_value,
                                         char** target) {
  Local<Array> js_array;
  uint32_t length;
  size_t list_size, data_size, data_offset;
  char** list;
  char* buffer;

  if (!js_value->IsArray())
    return UV_EINVAL;

  js_array = js_value.As<Array>()->Clone().As<Array>();
  length = js_array->Length();

  // Convert all array elements to string. Modify the js object itself if
  // needed - it's okay since we cloned the original object.
  for (uint32_t i = 0; i < length; i++) {
    if (!js_array->Get(i)->IsString())
      js_array->Set(i, js_array->Get(i)->ToString());
  }

  // Index has a pointer to every string element, plus one more for a final
  // null pointer.
  list_size = (length + 1) * sizeof *list;

  // Compute the length of all strings. Include room for null terminator
  // after every string. Align strings to cache lines.
  data_size = 0;
  for (uint32_t i = 0; i < length; i++) {
    data_size += StringBytes::StorageSize(js_array->Get(i), UTF8) + 1;
    data_size = ROUND_UP(data_size, sizeof(void*));
  }

  buffer = new char[list_size + data_size];

  list = reinterpret_cast<char**>(buffer);
  data_offset = list_size;

  for (uint32_t i = 0; i < length; i++) {
    list[i] = buffer + data_offset;
    data_offset += StringBytes::Write(buffer + data_offset,
                                      -1,
                                      js_array->Get(i),
                                      UTF8);
    buffer[data_offset++] = '\0';
    data_offset = ROUND_UP(data_offset, sizeof(void*));
  }

  list[length] = NULL;

  *target = buffer;
  return 0;
}


void SyncProcessRunner::ExitCallback(uv_process_t* handle,
                                     int64_t exit_status,
                                     int term_signal) {
  SyncProcessRunner* self = reinterpret_cast<SyncProcessRunner*>(handle->data);
  self->OnExit(exit_status, term_signal);
}


void SyncProcessRunner::KillTimerCallback(uv_timer_t* handle, int status) {
  SyncProcessRunner* self = reinterpret_cast<SyncProcessRunner*>(handle->data);
  self->OnKillTimerTimeout(status);
}


void SyncProcessRunner::KillTimerCloseCallback(uv_handle_t* handle) {
  // No-op.
}

}  // namespace node

NODE_MODULE_CONTEXT_AWARE(node_spawn_sync, node::SyncProcessRunner::Initialize)
