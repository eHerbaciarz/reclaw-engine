#include "AppEventQueue.h"

void AppEventQueue::Push(const AppEvent &e) {
  SdlLockGuard<SdlMutex> lock(m_mutex);
  m_queue.push_back(e);
}

bool AppEventQueue::Poll(AppEvent &out) {
  SdlLockGuard<SdlMutex> lock(m_mutex);
  if (m_queue.empty()) {
    return false;
  }
  out = m_queue.front();
  m_queue.pop_front();
  return true;
}

void AppEventQueue::Clear() {
  SdlLockGuard<SdlMutex> lock(m_mutex);
  m_queue.clear();
}
