// Copyright 2025 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#ifndef JALV_OSC_H
#define JALV_OSC_H

#include "attributes.h"

JALV_BEGIN_DECLS

typedef struct JalvImpl Jalv;

int
jalv_osc_start(Jalv* jalv);

void
jalv_osc_stop(Jalv* jalv);

JALV_END_DECLS

#endif // JALV_OSC_H
