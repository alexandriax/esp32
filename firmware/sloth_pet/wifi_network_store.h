#pragma once
#include <stddef.h>
namespace sloth { namespace wifi_network_store {
struct Credentials { char ssid[33]; char password[65]; };
enum class Result { Ok, Missing, Invalid, StorageError };
// Canonical network record. Only absence permits a read of the legacy paired
// profile; invalid records and a durable forget marker never fall back.
Result load(Credentials& credentials);
bool save(const Credentials& credentials);
bool forget();
void clear(Credentials& credentials);
} }
