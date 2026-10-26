#pragma once

#include "AppEvent.h"
#include <deque>
#include "../../Util/SdlMutex.h"

class AppEventQueue {
public:
  void Push(const AppEvent &e);
  bool Poll(AppEvent &out);
  void Clear();

private:
  SdlMutex m_mutex;
  std::deque<AppEvent> m_queue;
};
