// Copyright 2025 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "osc.h"

#include "control.h"
#include "jalv.h"
#include "log.h"

#if HAVE_LIBLO
#  include <lo/lo.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if HAVE_LIBLO

static int
osc_error(const int num, const char* const msg, const char* const where)
{
  fprintf(stderr, "OSC error %d in %s: %s\n", num, where, msg);
  return 0;
}

static const char*
property_leaf_name(const char* const uri)
{
  if (!uri) {
    return "";
  }

  const char* leaf = strrchr(uri, '#');
  if (leaf) {
    return leaf + 1;
  }

  leaf = strrchr(uri, '/');
  return leaf ? leaf + 1 : uri;
}

static bool
is_osc_control_name(const Control* const control, const char* const symbol)
{
  const char* const control_symbol = lilv_node_as_string(control->symbol);
  if (!strcmp(control_symbol, symbol)) {
    return true;
  }

  if (control->type == PROPERTY) {
    const char* const property_uri = lilv_node_as_uri(control->node);
    if (!strcmp(property_leaf_name(property_uri), symbol)) {
      return true;
    }
  }

  return false;
}

static Control*
osc_find_control(const Jalv* const jalv, const char* const symbol)
{
  for (size_t i = 0; i < jalv->controls.n_controls; ++i) {
    Control* const control = jalv->controls.controls[i];
    if (is_osc_control_name(control, symbol)) {
      return control;
    }
  }

  return NULL;
}

static int
osc_set_property_control(Jalv* const           jalv,
                         Control* const        control,
                         const char* const     types,
                         lo_arg** const        argv,
                         const int             argc,
                         const char* const     path)
{
  if (!types || !types[0]) {
    jalv_log(JALV_LOG_WARNING, "Ignoring OSC path %s with no type\n", path);
    return 0;
  }

  const char osc_type = types[0];

  if (control->value_type == jalv->forge.Float && osc_type == 'f' && argc >= 1) {
    const float value = argv[0]->f;
    return jalv_set_control(jalv, control, sizeof(value), jalv->forge.Float, &value);
  }

  if (control->value_type == jalv->forge.Double && osc_type == 'd' && argc >= 1) {
    const double value = argv[0]->d;
    return jalv_set_control(jalv, control, sizeof(value), jalv->forge.Double, &value);
  }

  if (control->value_type == jalv->forge.Int && osc_type == 'i' && argc >= 1) {
    const int32_t value = argv[0]->i;
    return jalv_set_control(jalv, control, sizeof(value), jalv->forge.Int, &value);
  }

  if (control->value_type == jalv->forge.Long && osc_type == 'h' && argc >= 1) {
    const int64_t value = argv[0]->h;
    return jalv_set_control(jalv, control, sizeof(value), jalv->forge.Long, &value);
  }

  if (control->value_type == jalv->forge.Bool && (osc_type == 'T' || osc_type == 'F')) {
    const int32_t value = (osc_type == 'T');
    return jalv_set_control(jalv, control, sizeof(value), jalv->forge.Bool, &value);
  }

  if ((control->value_type == jalv->forge.String ||
       control->value_type == jalv->forge.Path) &&
      (osc_type == 's' || osc_type == 'S') && argc >= 1) {
    const char* const value = &argv[0]->s;
    const size_t      size  = strlen(value) + 1U;
    return jalv_set_control(jalv, control, (uint32_t)size, control->value_type, value);
  }

  jalv_log(JALV_LOG_WARNING,
           "Ignoring OSC path %s with unsupported type '%c'\n",
           path,
           osc_type);
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
  Control* const    control = osc_find_control(jalv, symbol);
  if (!control) {
    jalv_log(JALV_LOG_WARNING, "Ignoring OSC for unknown control: %s\n", path);
    return 0;
  }

  if (control->type == PORT) {
    if (argc < 1 || !types || types[0] != 'f') {
      jalv_log(
        JALV_LOG_WARNING, "Ignoring OSC path %s with unsupported type\n", path);
      return 0;
    }

    const float value = argv[0]->f;
    jalv_set_control(jalv, control, sizeof(value), jalv->forge.Float, &value);
    return 0;
  }

  if (control->type != PROPERTY) {
    jalv_log(JALV_LOG_WARNING, "Ignoring OSC path %s with unsupported type\n", path);
    return 0;
  }

  return osc_set_property_control(jalv, control, types, argv, argc, path);
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
