#pragma once
void browser_mock_lock();void browser_mock_unlock();
#define LOCK_TCPIP_CORE() browser_mock_lock()
#define UNLOCK_TCPIP_CORE() browser_mock_unlock()
