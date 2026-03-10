// Copyright 2025 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "osc.h"

#include "control.h"
#include "jalv.h"
#include "log.h"

#if HAVE_LIBLO
#  include <lo/lo.h>
#endif

#include <stdio.h>
#include <string.h>

#if HAVE_LIBLO

static int
osc_error(const int num, const char* const msg, const char* const where)
{
  fprintf(stderr, "OSC error %d in %s: %s\n", num, where, msg);
  return 0;
}

static int
osc_control_handler(const char* const     path,
                    const char* const     types,
                    lo_arg** const        argv,
                    const int             argc,
                    lo_message            ZIX_UNUSED(msg),
                    void* const           user_data)
{
  Jalv* const jalv = (Jalv*)user_data;

  if (!jalv->osc_prefix_len || strncmp(path, jalv->osc_prefix, jalv->osc_prefix_len) ||
      path[jalv->osc_prefix_len] != '/') {
    return 1;
  }

  const char* const symbol = path + jalv->osc_prefix_len + 1;
  Control* const    control = get_named_control(&jalv->controls, symbol);
  if (!control || control->type != PORT) {
    jalv_log(JALV_LOG_WARNING, "Ignoring OSC for unknown control: %s\n", path);
    return 0;
  }

  if (argc < 1 || !types || types[0] != 'f') {
    jalv_log(JALV_LOG_WARNING, "Ignoring OSC path %s with unsupported type\n", path);
    return 0;
  }

  const float value = argv[0]->f;
  jalv_set_control(jalv, control, sizeof(value), jalv->forge.Float, &value);
  return 0;
}
#endif

int
jalv_osc_start(Jalv* const jalv)
{
#if HAVE_LIBLO
  char port_str[16] = {0};
  snprintf(port_str, sizeof(port_str), "%u", jalv->opts.osc_port);

  snprintf(
    jalv->osc_prefix, sizeof(jalv->osc_prefix), "/%s", jalv->opts.name);
  jalv->osc_prefix_len = strlen(jalv->osc_prefix);

  jalv->osc_server = lo_server_thread_new(port_str, osc_error);
  if (!jalv->osc_server) {
    jalv_log(JALV_LOG_WARNING,
             "Failed to start OSC listener on port %u\n",
             jalv->opts.osc_port);
    return 1;
  }

  lo_server_thread_add_method(
    jalv->osc_server, NULL, NULL, osc_control_handler, jalv);
  lo_server_thread_start(jalv->osc_server);

  jalv_log(JALV_LOG_INFO,
           "OSC listen:   udp://0.0.0.0:%u%s/<parameter>\n",
           jalv->opts.osc_port,
           jalv->osc_prefix);
#else
  if (jalv->opts.osc_port) {
    jalv_log(JALV_LOG_WARNING,
             "OSC requested, but this build has no liblo support\n");
  }
#endif

  return 0;
}

void
jalv_osc_stop(Jalv* const jalv)
{
#if HAVE_LIBLO
  if (jalv->osc_server) {
    lo_server_thread_stop(jalv->osc_server);
    lo_server_thread_free(jalv->osc_server);
    jalv->osc_server = NULL;
  }
#endif
}
