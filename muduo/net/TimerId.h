// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TIMERID_H
#define MUDUO_NET_TIMERID_H

#include "muduo/base/copyable.h"

namespace muduo
{
namespace net
{

class Timer;

///
/// An opaque identifier, for canceling Timer.
///
// TimerId唯一的标识了一个定时任务的ID
class TimerId : public muduo::copyable
{
 public:
 // 默认构造函数，设置timer_指针为NULL，sequence_为0
  TimerId()
    : timer_(NULL),
      sequence_(0)
  {
  }
// TimerId带参构造函数
  TimerId(Timer* timer, int64_t seq)
    : timer_(timer),
      sequence_(seq)
  {
  }

  // default copy-ctor, dtor and assignment are okay
// 友元类
  friend class TimerQueue;

 private:
 // 对应的定时任务
  Timer* timer_;
// 序列号
  int64_t sequence_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TIMERID_H
