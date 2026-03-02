#pragma once

#if __has_include_next(<nats/nats.h>)
#include_next <nats/nats.h>
#elif __has_include(<nats.h>)
#include <nats.h>
#else
#error "Unable to locate libnats headers (<nats/nats.h> or <nats.h>)."
#endif
