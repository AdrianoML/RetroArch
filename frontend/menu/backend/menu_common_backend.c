/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2014 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include "menu_common_backend.h"
#include "../menu_entries.h"
#include "../menu_navigation.h"
#include "../menu_input_line_cb.h"

#include "../../../gfx/gfx_common.h"
#include "../../../gfx/shader_common.h"
#include "../../../driver.h"
#include "../../../file_ext.h"
#include "../../../input/input_common.h"
#include "../../../config.def.h"
#include "../../../input/keyboard_line.h"

#include "../../../settings_data.h"

#if defined(HAVE_CG) || defined(HAVE_HLSL) || defined(HAVE_GLSL)
#define HAVE_SHADER_MANAGER
#endif

// FIXME: Ugly hack, nees to be refactored badly
static size_t hack_shader_pass = 0;

static void *get_last_setting(const file_list_t *list, int index,
      rarch_setting_t *settings)
{
   if (settings)
      return (rarch_setting_t*)setting_data_find_setting(settings,
            list->list[index].label);
   return NULL;
}

static int menu_info_screen_iterate(unsigned action)
{
   char msg[PATH_MAX];
   char needle[PATH_MAX];
   unsigned info_type = 0;
   rarch_setting_t *current_setting = NULL;
   rarch_setting_t *setting_data = (rarch_setting_t *)setting_data_get_list();

   if (!driver.menu || !setting_data)
      return 0;

   if (driver.video_data && driver.menu_ctx && driver.menu_ctx->render)
      driver.menu_ctx->render();

   current_setting = (rarch_setting_t*)get_last_setting(
         driver.menu->selection_buf,
         driver.menu->selection_ptr,
         setting_data_get_list());

   if (current_setting)
      strlcpy(needle, current_setting->name, sizeof(needle));
   else if ((current_setting = (rarch_setting_t*)get_last_setting(
            driver.menu->selection_buf,
            driver.menu->selection_ptr,
            setting_data_get_mainmenu(true))))
   {
      strlcpy(needle, current_setting->name, sizeof(needle));
   }
   else
   {
         const char *label = NULL;
         file_list_get_at_offset(driver.menu->selection_buf,
               driver.menu->selection_ptr, NULL, &label,
               &info_type);

         if (label)
            strlcpy(needle, label, sizeof(needle));
   }

   setting_data_get_description(needle, msg, sizeof(msg));

   if (driver.video_data && driver.menu_ctx &&
         driver.menu_ctx->render_messagebox)
   {
      if (*msg && msg[0] != '\0')
         driver.menu_ctx->render_messagebox(msg);
   }

   if (action == MENU_ACTION_OK)
      menu_entries_pop(driver.menu->menu_stack);

   return 0;
}

static int menu_start_screen_iterate(unsigned action)
{
   unsigned i;
   char msg[1024];

   if (!driver.menu)
      return 0;

   if (driver.video_data && driver.menu_ctx && driver.menu_ctx->render)
      driver.menu_ctx->render();

   static const unsigned binds[] = {
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RARCH_MENU_TOGGLE,
      RARCH_QUIT_KEY,
   };
   char desc[ARRAY_SIZE(binds)][64];

   for (i = 0; i < ARRAY_SIZE(binds); i++)
   {
      const struct retro_keybind *bind = (const struct retro_keybind*)
         &g_settings.input.binds[0][binds[i]];
      const struct retro_keybind *auto_bind = (const struct retro_keybind*)
         input_get_auto_bind(0, binds[i]);

      input_get_bind_string(desc[i], bind, auto_bind, sizeof(desc[i]));
   }

   snprintf(msg, sizeof(msg),
         "-- Welcome to RetroArch --\n"
         " \n" // strtok_r doesn't split empty strings.

         "Basic Menu controls:\n"
         "    Scroll (Up): %-20s\n"
         "  Scroll (Down): %-20s\n"
         "      Accept/OK: %-20s\n"
         "           Back: %-20s\n"
         "           Info: %-20s\n"
         "Enter/Exit Menu: %-20s\n"
         " Exit RetroArch: %-20s\n"
         " \n"

         "To run content:\n"
         "Load a libretro core (Core).\n"
         "Load a content file (Load Content).     \n"
         " \n"

         "See Path Options to set directories\n"
         "for faster access to files.\n"
         " \n"

         "Press Accept/OK to continue.",
         desc[0], desc[1], desc[2], desc[3], desc[4], desc[5], desc[6]);

   if (driver.video_data && driver.menu_ctx && driver.menu_ctx->render_messagebox)
      driver.menu_ctx->render_messagebox(msg);

   if (action == MENU_ACTION_OK)
      menu_entries_pop(driver.menu->menu_stack);

   return 0;
}

static int menu_common_core_setting_toggle(unsigned setting, unsigned action)
{
   unsigned index = setting - MENU_SETTINGS_CORE_OPTION_START;

   switch (action)
   {
      case MENU_ACTION_LEFT:
         core_option_prev(g_extern.system.core_options, index);
         break;

      case MENU_ACTION_RIGHT:
      case MENU_ACTION_OK:
         core_option_next(g_extern.system.core_options, index);
         break;

      case MENU_ACTION_START:
         core_option_set_default(g_extern.system.core_options, index);
         break;

      default:
         break;
   }

   return 0;
}

static int menu_common_setting_set_perf(unsigned setting, unsigned action,
      struct retro_perf_counter **counters, unsigned offset)
{
   if (counters[offset] && action == MENU_ACTION_START)
   {
      counters[offset]->total = 0;
      counters[offset]->call_cnt = 0;
   }
   return 0;
}

static void menu_common_setting_set_current_boolean(
      rarch_setting_t *setting, unsigned action)
{
   if (
         !strcmp(setting->name, "savestate") ||
         !strcmp(setting->name, "loadstate"))
   {
      if (action == MENU_ACTION_START)
         g_settings.state_slot = 0;
      else if (action == MENU_ACTION_LEFT)
      {
         // Slot -1 is (auto) slot.
         if (g_settings.state_slot >= 0)
            g_settings.state_slot--;
      }
      else if (action == MENU_ACTION_RIGHT)
         g_settings.state_slot++;
      else if (action == MENU_ACTION_OK)
         *setting->value.boolean = !(*setting->value.boolean);
   }
   else
   {
      switch (action)
      {
         case MENU_ACTION_OK:
         case MENU_ACTION_LEFT:
         case MENU_ACTION_RIGHT:
            *setting->value.boolean = !(*setting->value.boolean);
            break;
         case MENU_ACTION_START:
            *setting->value.boolean = setting->default_value.boolean;
            break;
      }
   }

   if (setting->change_handler)
      setting->change_handler(setting);
}

static void menu_common_setting_set_current_path_selection(
      rarch_setting_t *setting, const char *start_path,
      const char *label, unsigned type,
      unsigned action)
{
   switch (action)
   {
      case MENU_ACTION_OK:
         menu_entries_push(driver.menu->menu_stack,
               start_path, label, type,
               driver.menu->selection_ptr);
         break;
      case MENU_ACTION_START:
         *setting->value.string = '\0';
         break;
   }

   if (setting->change_handler)
      setting->change_handler(setting);
}

static void menu_common_setting_set_current_fraction(
      rarch_setting_t *setting, unsigned action)
{
   if (!strcmp(setting->name, "video_refresh_rate_auto"))
   {
      if (action == MENU_ACTION_START)
         g_extern.measure_data.frame_time_samples_count = 0;
      else if (action == MENU_ACTION_OK)
      {
         double refresh_rate, deviation = 0.0;
         unsigned sample_points = 0;

         if (driver_monitor_fps_statistics(&refresh_rate,
                  &deviation, &sample_points))
         {
            driver_set_monitor_refresh_rate(refresh_rate);
            /* Incase refresh rate update forced non-block video. */
            rarch_main_command(RARCH_CMD_VIDEO_SET_BLOCKING_STATE);
         }
      }
   }
   else if (!strcmp(setting->name, "fastforward_ratio"))
   {
      bool clamp_value = false;
      if (action == MENU_ACTION_START)
        *setting->value.fraction  = setting->default_value.fraction;
      else if (action == MENU_ACTION_LEFT)
      {
         *setting->value.fraction -= setting->step;

         /* Avoid potential rounding errors when going from 1.1 to 1.0. */
         if (*setting->value.fraction < 0.95f) 
            *setting->value.fraction = setting->default_value.fraction;
         else
            clamp_value = true;
      }
      else if (action == MENU_ACTION_RIGHT)
      {
         *setting->value.fraction += setting->step;
         clamp_value = true;
      }
      if (clamp_value)
         g_settings.fastforward_ratio =
            max(min(*setting->value.fraction, setting->max), 1.0f);
   }
   else
   {
      switch (action)
      {
         case MENU_ACTION_LEFT:
            *setting->value.fraction =
               *setting->value.fraction - setting->step;

            if (setting->enforce_minrange)
            {
               if (*setting->value.fraction < setting->min)
                  *setting->value.fraction = setting->min;
            }
            break;

         case MENU_ACTION_RIGHT:
         case MENU_ACTION_OK:
            *setting->value.fraction = 
               *setting->value.fraction + setting->step;

            if (setting->enforce_maxrange)
            {
               if (*setting->value.fraction > setting->max)
                  *setting->value.fraction = setting->max;
            }
            break;

         case MENU_ACTION_START:
            *setting->value.fraction = setting->default_value.fraction;
            break;
      }
   }

   if (setting->change_handler)
      setting->change_handler(setting);
}

static void menu_common_setting_set_current_unsigned_integer(
      rarch_setting_t *setting, unsigned id, unsigned action)
{
   if (id == MENU_FILE_LINEFEED)
   {
      if (action == MENU_ACTION_OK)
         menu_key_start_line(driver.menu, setting->short_description,
               setting->name, st_uint_callback);
      else if (action == MENU_ACTION_START)
         *setting->value.unsigned_integer =
            setting->default_value.unsigned_integer;
   }
   else
   {
      switch (action)
      {
         case MENU_ACTION_LEFT:
            if (*setting->value.unsigned_integer != setting->min)
               *setting->value.unsigned_integer =
                  *setting->value.unsigned_integer - setting->step;

            if (setting->enforce_minrange)
            {
               if (*setting->value.unsigned_integer < setting->min)
                  *setting->value.unsigned_integer = setting->min;
            }
            break;

         case MENU_ACTION_RIGHT:
         case MENU_ACTION_OK:
            *setting->value.unsigned_integer =
               *setting->value.unsigned_integer + setting->step;

            if (setting->enforce_maxrange)
            {
               if (*setting->value.unsigned_integer > setting->max)
                  *setting->value.unsigned_integer = setting->max;
            }
            break;

         case MENU_ACTION_START:
            *setting->value.unsigned_integer =
               setting->default_value.unsigned_integer;
            break;
      }
   } 

   if (setting->change_handler)
      setting->change_handler(setting);
}


static void menu_common_setting_set_current_string_path(
      rarch_setting_t *setting, const char *dir, const char *path)
{
   fill_pathname_join(setting->value.string, dir, path, setting->size);

   if (setting->change_handler)
      setting->change_handler(setting);
}

void menu_common_set_current_string_based_on_label(
      const char *label, const char *str)
{
   if (!strcmp(label, "video_shader_preset_save_as"))
   {
#ifdef HAVE_SHADER_MANAGER
      if (driver.menu_ctx && driver.menu_ctx->backend
            && driver.menu_ctx->backend->shader_manager_save_preset)
         driver.menu_ctx->backend->shader_manager_save_preset(str, false);
#endif
   }
}

void menu_common_setting_set_current_string(
      rarch_setting_t *setting, const char *str)
{
   strlcpy(setting->value.string, str, setting->size);

   if (setting->change_handler)
      setting->change_handler(setting);
}

static void handle_driver(const char *label, char *driver,
      size_t sizeof_driver, unsigned action)
{
   switch (action)
   {
      case MENU_ACTION_LEFT:
         find_prev_driver(label, driver, sizeof_driver);
         break;
      case MENU_ACTION_RIGHT:
         find_next_driver(label, driver, sizeof_driver);
         break;
   }
}

static void handle_setting(rarch_setting_t *setting,
      unsigned id, const char *label, unsigned action)
{
   if (setting->type == ST_BOOL)
      menu_common_setting_set_current_boolean(setting, action);
   else if (setting->type == ST_UINT)
      menu_common_setting_set_current_unsigned_integer(setting, id, action);
   else if (setting->type == ST_FLOAT)
      menu_common_setting_set_current_fraction(setting, action);
   else if (setting->type == ST_DIR)
   {
      if (action == MENU_ACTION_START)
      {
         *setting->value.string = '\0';

         if (setting->change_handler)
            setting->change_handler(setting);
      }
   }
   else if (setting->type == ST_PATH)
      menu_common_setting_set_current_path_selection(setting,
            setting->default_value.string, setting->name, id, action);
   else if (setting->type == ST_STRING)
   {
      if (id == MENU_FILE_LINEFEED || id == MENU_FILE_LINEFEED_SWITCH)
      {
         if (action == MENU_ACTION_OK)
            menu_key_start_line(driver.menu, setting->short_description,
                  setting->name, st_string_callback);
         else if (action == MENU_ACTION_START)
            *setting->value.string = '\0';
      }
      else if (!strcmp(setting->name, "video_driver"))
         handle_driver(setting->name, g_settings.video.driver,
               sizeof(g_settings.video.driver), action);
      else if (!strcmp(setting->name, "audio_driver"))
         handle_driver(setting->name, g_settings.audio.driver,
               sizeof(g_settings.audio.driver), action);
      else if (!strcmp(setting->name, "audio_resampler_driver"))
      {
         if (action == MENU_ACTION_LEFT)
            find_prev_resampler_driver();
         else if (action == MENU_ACTION_RIGHT)
            find_next_resampler_driver();
      }
      else if (!strcmp(setting->name, "input_driver"))
         handle_driver(setting->name, g_settings.input.driver,
               sizeof(g_settings.input.driver), action);
      else if (!strcmp(setting->name, "camera_driver"))
         handle_driver(setting->name, g_settings.camera.driver,
               sizeof(g_settings.camera.driver), action);
      else if (!strcmp(setting->name, "location_driver"))
         handle_driver(setting->name, g_settings.location.driver,
               sizeof(g_settings.location.driver), action);
      else if (!strcmp(setting->name, "menu_driver"))
         handle_driver(setting->name, g_settings.menu.driver,
               sizeof(g_settings.menu.driver), action);
   }
}

static int menu_setting_set(unsigned id, const char *label,
      unsigned action)
{
   unsigned port = driver.menu->current_pad;
   rarch_setting_t *setting = (rarch_setting_t*)get_last_setting(
         driver.menu->selection_buf, driver.menu->selection_ptr,
         setting_data_get_list()
         );

   if (setting)
      handle_setting(setting, id, label, action);
   else
   {
      setting = (rarch_setting_t*)get_last_setting(
            driver.menu->selection_buf, driver.menu->selection_ptr,
            setting_data_get_mainmenu(true)
            );

      if (setting)
      {
         if (!strcmp(setting->name, "disk_index"))
         {
            int step = 0;

            if (action == MENU_ACTION_RIGHT || action == MENU_ACTION_OK)
               step = 1;
            else if (action == MENU_ACTION_LEFT)
               step = -1;

            if (step)
            {
               const struct retro_disk_control_callback *control =
                  (const struct retro_disk_control_callback*)
                  &g_extern.system.disk_control;
               unsigned num_disks = control->get_num_images();
               unsigned current   = control->get_image_index();
               unsigned next_index = (current + num_disks + 1 + step)
                  % (num_disks + 1);
               rarch_disk_control_set_eject(true, false);
               rarch_disk_control_set_index(next_index);
               rarch_disk_control_set_eject(false, false);
            }
         }

         handle_setting(setting, id, label, action);
      }
      else if (!strcmp(label, "video_shader_num_passes"))
      {
#ifdef HAVE_SHADER_MANAGER
         struct gfx_shader *shader = (struct gfx_shader*)
            driver.menu->shader;

         switch (action)
         {
            case MENU_ACTION_START:
               if (shader && shader->passes)
                  shader->passes = 0;
               driver.menu->need_refresh = true;
               break;

            case MENU_ACTION_LEFT:
               if (shader && shader->passes)
                  shader->passes--;
               driver.menu->need_refresh = true;
               break;

            case MENU_ACTION_RIGHT:
            case MENU_ACTION_OK:
               if (shader && (shader->passes < GFX_MAX_SHADERS))
                  shader->passes++;
               driver.menu->need_refresh = true;
               break;

            default:
               break;
         }

         if (driver.menu->need_refresh)
            gfx_shader_resolve_parameters(NULL, driver.menu->shader);
#endif
      }
      else if (!strcmp(label, "input_bind_device_id"))
      {
         int *p = &g_settings.input.joypad_map[port];
         if (action == MENU_ACTION_START)
            *p = port;
         else if (action == MENU_ACTION_LEFT)
            (*p)--;
         else if (action == MENU_ACTION_RIGHT)
            (*p)++;

         if (*p < -1)
            *p = -1;
         else if (*p >= MAX_PLAYERS)
            *p = MAX_PLAYERS - 1;
      }
      else if (!strcmp(label, "input_bind_device_type"))
      {
         unsigned current_device, current_index, i, devices[128];
         const struct retro_controller_info *desc;
         unsigned types = 0;

         devices[types++] = RETRO_DEVICE_NONE;
         devices[types++] = RETRO_DEVICE_JOYPAD;

         /* Only push RETRO_DEVICE_ANALOG as default if we use an 
          * older core which doesn't use SET_CONTROLLER_INFO. */
         if (!g_extern.system.num_ports)
            devices[types++] = RETRO_DEVICE_ANALOG;

         desc = port < g_extern.system.num_ports ?
            &g_extern.system.ports[port] : NULL;
         if (desc)
         {
            for (i = 0; i < desc->num_types; i++)
            {
               unsigned id = desc->types[i].id;
               if (types < ARRAY_SIZE(devices) &&
                     id != RETRO_DEVICE_NONE &&
                     id != RETRO_DEVICE_JOYPAD)
                  devices[types++] = id;
            }
         }

         current_device = g_settings.input.libretro_device[port];
         current_index = 0;
         for (i = 0; i < types; i++)
         {
            if (current_device == devices[i])
            {
               current_index = i;
               break;
            }
         }

         bool updated = true;
         switch (action)
         {
            case MENU_ACTION_START:
               current_device = RETRO_DEVICE_JOYPAD;
               break;

            case MENU_ACTION_LEFT:
               current_device = devices
                  [(current_index + types - 1) % types];
               break;

            case MENU_ACTION_RIGHT:
            case MENU_ACTION_OK:
               current_device = devices
                  [(current_index + 1) % types];
               break;

            default:
               updated = false;
         }

         if (updated)
         {
            g_settings.input.libretro_device[port] = current_device;
            pretro_set_controller_port_device(port, current_device);
         }

      }
      else
      {
         switch (id)
         {
            case MENU_SETTINGS_BIND_PLAYER:
               if (action == MENU_ACTION_START)
                  driver.menu->current_pad = 0;
               else if (action == MENU_ACTION_LEFT)
               {
                  if (driver.menu->current_pad != 0)
                     driver.menu->current_pad--;
               }
               else if (action == MENU_ACTION_RIGHT)
               {
                  if (driver.menu->current_pad < MAX_PLAYERS - 1)
                     driver.menu->current_pad++;
               }
               if (port != driver.menu->current_pad)
                  driver.menu->need_refresh = true;
               port = driver.menu->current_pad;
               break;
            case MENU_SETTINGS_BIND_ANALOG_MODE:
               switch (action)
               {
                  case MENU_ACTION_START:
                     g_settings.input.analog_dpad_mode[port] = 0;
                     break;

                  case MENU_ACTION_OK:
                  case MENU_ACTION_RIGHT:
                     g_settings.input.analog_dpad_mode[port] =
                        (g_settings.input.analog_dpad_mode[port] + 1)
                        % ANALOG_DPAD_LAST;
                     break;

                  case MENU_ACTION_LEFT:
                     g_settings.input.analog_dpad_mode[port] =
                        (g_settings.input.analog_dpad_mode
                         [port] + ANALOG_DPAD_LAST - 1) % ANALOG_DPAD_LAST;
                     break;

                  default:
                     break;
               }
               break;
            case MENU_SETTINGS_CUSTOM_BIND_MODE:
               if (action == MENU_ACTION_LEFT || action == MENU_ACTION_RIGHT)
                  driver.menu->bind_mode_keyboard =
                    !driver.menu->bind_mode_keyboard;
               break;
#if defined(GEKKO)
            case MENU_SETTINGS_VIDEO_RESOLUTION:
               if (action == MENU_ACTION_LEFT)
               {
                  if (menu_current_gx_resolution > 0)
                     menu_current_gx_resolution--;
               }
               else if (action == MENU_ACTION_RIGHT)
               {
                  if (menu_current_gx_resolution < GX_RESOLUTIONS_LAST - 1)
                  {
#ifdef HW_RVL
                     if ((menu_current_gx_resolution + 1) > GX_RESOLUTIONS_640_480)
                        if (CONF_GetVideo() != CONF_VIDEO_PAL)
                           return 0;
#endif

                     menu_current_gx_resolution++;
                  }
               }
               else if (action == MENU_ACTION_OK)
               {
                  if (driver.video_data)
                     gx_set_video_mode(driver.video_data, menu_gx_resolutions
                           [menu_current_gx_resolution][0],
                           menu_gx_resolutions[menu_current_gx_resolution][1]);
               }
               break;
#elif defined(__CELLOS_LV2__)
            case MENU_SETTINGS_VIDEO_RESOLUTION:
               if (action == MENU_ACTION_LEFT)
               {
                  if (g_extern.console.screen.resolutions.current.idx)
                  {
                     g_extern.console.screen.resolutions.current.idx--;
                     g_extern.console.screen.resolutions.current.id =
                        g_extern.console.screen.resolutions.list
                        [g_extern.console.screen.resolutions.current.idx];
                  }
               }
               else if (action == MENU_ACTION_RIGHT)
               {
                  if (g_extern.console.screen.resolutions.current.idx + 1 <
                        g_extern.console.screen.resolutions.count)
                  {
                     g_extern.console.screen.resolutions.current.idx++;
                     g_extern.console.screen.resolutions.current.id =
                        g_extern.console.screen.resolutions.list
                        [g_extern.console.screen.resolutions.current.idx];
                  }
               }
               else if (action == MENU_ACTION_OK)
               {
                  if (g_extern.console.screen.resolutions.list[
                        g_extern.console.screen.resolutions.current.idx] == 
                        CELL_VIDEO_OUT_RESOLUTION_576)
                  {
                     if (g_extern.console.screen.pal_enable)
                        g_extern.console.screen.pal60_enable = true;
                  }
                  else
                  {
                     g_extern.console.screen.pal_enable = false;
                     g_extern.console.screen.pal60_enable = false;
                  }

                  rarch_main_command(RARCH_CMD_REINIT);
               }
#endif
               break;
            default:
               break;
         }
      }
   }

   return 0;
}

#ifdef HAVE_SHADER_MANAGER
#include "menu_common_shader_backend.c"
#endif

static int menu_setting_ok_toggle(unsigned type,
      const char *dir, const char *label,
      unsigned action)
{
   if (type == MENU_SETTINGS_CUSTOM_BIND_ALL)
   {
      driver.menu->binds.target = &g_settings.input.binds
         [driver.menu->current_pad][0];
      driver.menu->binds.begin = MENU_SETTINGS_BIND_BEGIN;
      driver.menu->binds.last = MENU_SETTINGS_BIND_LAST;

      file_list_push(driver.menu->menu_stack, "", "",
            driver.menu->bind_mode_keyboard ?
            MENU_SETTINGS_CUSTOM_BIND_KEYBOARD :
            MENU_SETTINGS_CUSTOM_BIND,
            driver.menu->selection_ptr);
      if (driver.menu->bind_mode_keyboard)
      {
         driver.menu->binds.timeout_end =
            rarch_get_time_usec() + 
            MENU_KEYBOARD_BIND_TIMEOUT_SECONDS * 1000000;
         input_keyboard_wait_keys(driver.menu,
               menu_custom_bind_keyboard_cb);
      }
      else
      {
         menu_poll_bind_get_rested_axes(&driver.menu->binds);
         menu_poll_bind_state(&driver.menu->binds);
      }
      return 0;
   }
#ifdef HAVE_SHADER_MANAGER
   else if (!strcmp(label, "video_shader_preset_save_as"))
   {
      if (action == MENU_ACTION_OK)
         menu_key_start_line(driver.menu, "Preset Filename",
               label, st_string_callback);
   }
   else if (!strcmp(label, "shader_apply_changes"))
   {
      rarch_main_command(RARCH_CMD_SHADERS_APPLY_CHANGES);
      return 0;
   }
#endif
   else if (type == MENU_SETTINGS_CUSTOM_BIND_DEFAULT_ALL)
   {
      unsigned i;
      struct retro_keybind *target = (struct retro_keybind*)
         &g_settings.input.binds[driver.menu->current_pad][0];
      const struct retro_keybind *def_binds = 
         driver.menu->current_pad ? retro_keybinds_rest : retro_keybinds_1;

      driver.menu->binds.begin = MENU_SETTINGS_BIND_BEGIN;
      driver.menu->binds.last = MENU_SETTINGS_BIND_LAST;

      for (i = MENU_SETTINGS_BIND_BEGIN;
            i <= MENU_SETTINGS_BIND_LAST; i++, target++)
      {
         if (driver.menu->bind_mode_keyboard)
            target->key = def_binds[i - MENU_SETTINGS_BIND_BEGIN].key;
         else
         {
            target->joykey = NO_BTN;
            target->joyaxis = AXIS_NONE;
         }
      }
      return 0;
   }
   else if (type >= MENU_SETTINGS_BIND_BEGIN &&
         type <= MENU_SETTINGS_BIND_ALL_LAST)
   {
      struct retro_keybind *bind = (struct retro_keybind*)
         &g_settings.input.binds[driver.menu->current_pad]
         [type - MENU_SETTINGS_BIND_BEGIN];

      driver.menu->binds.begin  = type;
      driver.menu->binds.last   = type;
      driver.menu->binds.target = bind;
      driver.menu->binds.player = driver.menu->current_pad;
      file_list_push(driver.menu->menu_stack, "", "",
            driver.menu->bind_mode_keyboard ?
            MENU_SETTINGS_CUSTOM_BIND_KEYBOARD : MENU_SETTINGS_CUSTOM_BIND,
            driver.menu->selection_ptr);

      if (driver.menu->bind_mode_keyboard)
      {
         driver.menu->binds.timeout_end = rarch_get_time_usec() +
            MENU_KEYBOARD_BIND_TIMEOUT_SECONDS * 1000000;
         input_keyboard_wait_keys(driver.menu,
               menu_custom_bind_keyboard_cb);
      }
      else
      {
         menu_poll_bind_get_rested_axes(&driver.menu->binds);
         menu_poll_bind_state(&driver.menu->binds);
      }

      return 0;
   }
   else if (
         !strcmp(label, "load_content") ||
         !strcmp(label, "detect_core_list")
      )
   {
      menu_entries_push(driver.menu->menu_stack,
            g_settings.menu_content_directory, label, MENU_FILE_DIRECTORY,
            driver.menu->selection_ptr);
      return 0;
   }
   else if (!strcmp(label, "history_list") ||
         menu_common_type_is(label, type) == MENU_FILE_DIRECTORY
         )
   {
      menu_entries_push(driver.menu->menu_stack,
            "", label, type, driver.menu->selection_ptr);
      return 0;
   }
   else if (
         menu_common_type_is(label, type) == MENU_SETTINGS ||
         !strcmp(label, "core_list") ||
         !strcmp(label, "configurations") ||
         !strcmp(label, "disk_image_append")
         )
   {
      menu_entries_push(driver.menu->menu_stack,
            dir ? dir : label, label, type,
            driver.menu->selection_ptr);
      return 0;
   }
   else if (type == MENU_SETTINGS_CUSTOM_VIEWPORT)
   {
      file_list_push(driver.menu->menu_stack, "", "",
            MENU_SETTINGS_CUSTOM_VIEWPORT,
            driver.menu->selection_ptr);

      /* Start with something sane. */
      rarch_viewport_t *custom = (rarch_viewport_t*)
         &g_extern.console.screen.viewports.custom_vp;

      if (driver.video_data && driver.video &&
            driver.video->viewport_info)
         driver.video->viewport_info(driver.video_data, custom);
      aspectratio_lut[ASPECT_RATIO_CUSTOM].value =
         (float)custom->width / custom->height;

      g_settings.video.aspect_ratio_idx = ASPECT_RATIO_CUSTOM;

      rarch_main_command(RARCH_CMD_VIDEO_SET_ASPECT_RATIO);
      return 0;
   }
   return -1;
}

static int menu_setting_start_toggle(unsigned type,
      const char *dir, const char *label,
      unsigned action)
{
   if (type >= MENU_SETTINGS_BIND_BEGIN &&
         type <= MENU_SETTINGS_BIND_ALL_LAST)
   {
      struct retro_keybind *bind = (struct retro_keybind*)
         &g_settings.input.binds[driver.menu->current_pad]
         [type - MENU_SETTINGS_BIND_BEGIN];

      if (driver.menu->bind_mode_keyboard)
      {
         const struct retro_keybind *def_binds = driver.menu->current_pad ?
            retro_keybinds_rest : retro_keybinds_1;
         bind->key = def_binds[type - MENU_SETTINGS_BIND_BEGIN].key;
      }
      else
      {
         bind->joykey = NO_BTN;
         bind->joyaxis = AXIS_NONE;
      }

      return 0;
   }
   return -1;
}

static int menu_setting_toggle(unsigned type,
      const char *dir, const char *label,
      unsigned action)
{
   struct retro_perf_counter **counters = NULL;

   if ((menu_common_type_is(label, type) == MENU_SETTINGS_SHADER_OPTIONS) ||
         !strcmp(label, "video_shader_parameters") ||
         !strcmp(label, "video_shader_preset_parameters")
         )
   {
      if (driver.menu_ctx && driver.menu_ctx->backend
            && driver.menu_ctx->backend->shader_manager_setting_toggle)
         return driver.menu_ctx->backend->shader_manager_setting_toggle(
               type, label, action);
   }
   else if ((type >= MENU_SETTINGS_CORE_OPTION_START))
      return menu_common_core_setting_toggle(type, action);
   else if (type >= MENU_SETTINGS_PERF_COUNTERS_BEGIN &&
         type <= MENU_SETTINGS_PERF_COUNTERS_END)
   {
      counters = (struct retro_perf_counter**)perf_counters_rarch;
      return menu_common_setting_set_perf(type, action, counters,
            type - MENU_SETTINGS_PERF_COUNTERS_BEGIN);
   }
   else if (type >= MENU_SETTINGS_LIBRETRO_PERF_COUNTERS_BEGIN &&
         type <= MENU_SETTINGS_LIBRETRO_PERF_COUNTERS_END)
   {
      counters = (struct retro_perf_counter**)perf_counters_libretro;
      return menu_common_setting_set_perf(type, action, counters,
            type - MENU_SETTINGS_LIBRETRO_PERF_COUNTERS_BEGIN);
   }
   else if (driver.menu_ctx && driver.menu_ctx->backend)
      return menu_setting_set(type, label, action);

   return 0;
}

static int menu_settings_iterate(unsigned action)
{
   const char *path = NULL;
   const char *dir  = NULL;
   const char *label = NULL;
   unsigned type = 0;
   unsigned menu_type = 0;

   driver.menu->frame_buf_pitch = driver.menu->width * 2;

   if (action != MENU_ACTION_REFRESH)
      file_list_get_at_offset(driver.menu->selection_buf,
            driver.menu->selection_ptr, NULL, &label, &type);

   if (!strcmp(label, "core_list"))
      dir = g_settings.libretro_directory;
   else if (!strcmp(label, "configurations"))
      dir = g_settings.menu_config_directory;
   else if (!strcmp(label, "disk_image_append"))
      dir = g_settings.menu_content_directory;

   if (driver.menu->need_refresh && action != MENU_ACTION_MESSAGE)
      action = MENU_ACTION_NOOP;

   switch (action)
   {
      case MENU_ACTION_UP:
         if (driver.menu->selection_ptr > 0)
            menu_decrement_navigation(driver.menu);
         else
            menu_set_navigation(driver.menu,
                  file_list_get_size(driver.menu->selection_buf) - 1);
         break;

      case MENU_ACTION_DOWN:
         if ((driver.menu->selection_ptr + 1) <
               file_list_get_size(driver.menu->selection_buf))
            menu_increment_navigation(driver.menu);
         else
            menu_clear_navigation(driver.menu);
         break;

      case MENU_ACTION_CANCEL:
         menu_entries_pop(driver.menu->menu_stack);
         break;
      case MENU_ACTION_SELECT:
         file_list_push(driver.menu->menu_stack, "", "info_screen",
               0, driver.menu->selection_ptr);
         break;
      case MENU_ACTION_OK:
         if (menu_setting_ok_toggle(type, dir, label, action) == 0)
            return 0;
         /* fall-through */
      case MENU_ACTION_START:
         if (menu_setting_start_toggle(type, dir, label, action) == 0)
            return 0;
         /* fall-through */
      case MENU_ACTION_LEFT:
      case MENU_ACTION_RIGHT:
         {
            int ret = menu_setting_toggle(type, dir,
                  label, action);

            if (ret)
               return ret;
         }
         break;

      case MENU_ACTION_REFRESH:
         menu_clear_navigation(driver.menu);
         driver.menu->need_refresh = true;
         break;

      case MENU_ACTION_MESSAGE:
         driver.menu->msg_force = true;
         break;

      default:
         break;
   }

   file_list_get_last(driver.menu->menu_stack, &path, &label, &menu_type);

   if (driver.menu->need_refresh && (menu_parse_check(label, menu_type) == -1))
   {
      driver.menu->need_refresh = false;
      menu_entries_push_list(driver.menu,
            driver.menu->selection_buf, path, label, menu_type);
   }

   if (driver.menu_ctx && driver.menu_ctx->render)
      driver.menu_ctx->render();

   /* Have to defer it so we let settings refresh. */
   if (driver.menu->push_start_screen)
   {
      driver.menu->push_start_screen = false;
      file_list_push(driver.menu->menu_stack, "", "help", 0, 0);
   }

   return 0;
}

static int menu_viewport_iterate(unsigned action)
{
   int stride_x = 1, stride_y = 1;
   char msg[64];
   struct retro_game_geometry *geom = NULL;
   const char *base_msg = NULL;
   const char *label = NULL;
   unsigned menu_type = 0;
   rarch_viewport_t *custom = (rarch_viewport_t*)
      &g_extern.console.screen.viewports.custom_vp;

   file_list_get_last(driver.menu->menu_stack, NULL, &label, &menu_type);

   geom = (struct retro_game_geometry*)&g_extern.system.av_info.geometry;

   if (g_settings.video.scale_integer)
   {
      stride_x = geom->base_width;
      stride_y = geom->base_height;
   }

   switch (action)
   {
      case MENU_ACTION_UP:
         if (menu_type == MENU_SETTINGS_CUSTOM_VIEWPORT)
         {
            custom->y -= stride_y;
            custom->height += stride_y;
         }
         else if (custom->height >= (unsigned)stride_y)
            custom->height -= stride_y;

         rarch_main_command(RARCH_CMD_VIDEO_APPLY_STATE_CHANGES);
         break;

      case MENU_ACTION_DOWN:
         if (menu_type == MENU_SETTINGS_CUSTOM_VIEWPORT)
         {
            custom->y += stride_y;
            if (custom->height >= (unsigned)stride_y)
               custom->height -= stride_y;
         }
         else
            custom->height += stride_y;

         rarch_main_command(RARCH_CMD_VIDEO_APPLY_STATE_CHANGES);
         break;

      case MENU_ACTION_LEFT:
         if (menu_type == MENU_SETTINGS_CUSTOM_VIEWPORT)
         {
            custom->x -= stride_x;
            custom->width += stride_x;
         }
         else if (custom->width >= (unsigned)stride_x)
            custom->width -= stride_x;

         rarch_main_command(RARCH_CMD_VIDEO_APPLY_STATE_CHANGES);
         break;

      case MENU_ACTION_RIGHT:
         if (menu_type == MENU_SETTINGS_CUSTOM_VIEWPORT)
         {
            custom->x += stride_x;
            if (custom->width >= (unsigned)stride_x)
               custom->width -= stride_x;
         }
         else
            custom->width += stride_x;

         rarch_main_command(RARCH_CMD_VIDEO_APPLY_STATE_CHANGES);
         break;

      case MENU_ACTION_CANCEL:
         menu_entries_pop(driver.menu->menu_stack);
         if (!strcmp(label, "custom_viewport_2"))
         {
            file_list_push(driver.menu->menu_stack, "", "",
                  MENU_SETTINGS_CUSTOM_VIEWPORT,
                  driver.menu->selection_ptr);
         }
         break;

      case MENU_ACTION_OK:
         menu_entries_pop(driver.menu->menu_stack);
         if (menu_type == MENU_SETTINGS_CUSTOM_VIEWPORT
               && !g_settings.video.scale_integer)
         {
            file_list_push(driver.menu->menu_stack, "",
                  "custom_viewport_2", 0, driver.menu->selection_ptr);
         }
         break;

      case MENU_ACTION_START:
         if (!g_settings.video.scale_integer)
         {
            rarch_viewport_t vp;

            if (driver.video_data && driver.video &&
                  driver.video->viewport_info)
               driver.video->viewport_info(driver.video_data, &vp);

            if (menu_type == MENU_SETTINGS_CUSTOM_VIEWPORT)
            {
               custom->width += custom->x;
               custom->height += custom->y;
               custom->x = 0;
               custom->y = 0;
            }
            else
            {
               custom->width = vp.full_width - custom->x;
               custom->height = vp.full_height - custom->y;
            }

            rarch_main_command(RARCH_CMD_VIDEO_APPLY_STATE_CHANGES);
         }
         break;

      case MENU_ACTION_MESSAGE:
         driver.menu->msg_force = true;
         break;

      default:
         break;
   }

   file_list_get_last(driver.menu->menu_stack, NULL, &label, &menu_type);

   if (driver.video_data && driver.menu_ctx && driver.menu_ctx->render)
      driver.menu_ctx->render();

   if (g_settings.video.scale_integer)
   {
      custom->x = 0;
      custom->y = 0;
      custom->width = ((custom->width + geom->base_width - 1) /
            geom->base_width) * geom->base_width;
      custom->height = ((custom->height + geom->base_height - 1) /
            geom->base_height) * geom->base_height;

      base_msg = "Set scale";
      snprintf(msg, sizeof(msg), "%s (%4ux%4u, %u x %u scale)",
            base_msg,
            custom->width, custom->height,
            custom->width / geom->base_width,
            custom->height / geom->base_height);
   }
   else
   {
      if (menu_type == MENU_SETTINGS_CUSTOM_VIEWPORT)
         base_msg = "Set Upper-Left Corner";
      else if (!strcmp(label, "custom_viewport_2"))
         base_msg = "Set Bottom-Right Corner";

      snprintf(msg, sizeof(msg), "%s (%d, %d : %4ux%4u)",
            base_msg, custom->x, custom->y, custom->width, custom->height);
   }

   if (driver.video_data && driver.menu_ctx &&
         driver.menu_ctx->render_messagebox)
      driver.menu_ctx->render_messagebox(msg);

   if (!custom->width)
      custom->width = stride_x;
   if (!custom->height)
      custom->height = stride_y;

   aspectratio_lut[ASPECT_RATIO_CUSTOM].value =
      (float)custom->width / custom->height;

   rarch_main_command(RARCH_CMD_VIDEO_APPLY_STATE_CHANGES);

   return 0;
}

static int menu_custom_bind_iterate(void *data)
{
   menu_handle_t *menu = (menu_handle_t*)data;
   struct menu_bind_state binds = menu->binds;

   menu_poll_bind_state(&binds);

   if ((binds.skip && !menu->binds.skip) ||
         menu_poll_find_trigger(&menu->binds, &binds))
   {
      binds.begin++;
      if (binds.begin <= binds.last)
         binds.target++;
      else
         menu_entries_pop(driver.menu->menu_stack);

      /* Avoid new binds triggering things right away. */
      menu->trigger_state = 0;
      menu->old_input_state = -1ULL;
   }
   menu->binds = binds;

   return 0;
}

static int menu_custom_bind_iterate_keyboard(void *data,
      int64_t current, int timeout)
{
   bool timed_out = false;
   menu_handle_t *menu = (menu_handle_t*)data;

   if (timeout <= 0)
   {
      menu->binds.begin++;

      /* Could be unsafe, but whatever. */
      menu->binds.target->key = RETROK_UNKNOWN;

      menu->binds.target++;
      menu->binds.timeout_end = rarch_get_time_usec() +
         MENU_KEYBOARD_BIND_TIMEOUT_SECONDS * 1000000;
      timed_out = true;
   }

   /* binds.begin is updated in keyboard_press callback. */
   if (menu->binds.begin > menu->binds.last)
   {
      menu_entries_pop(driver.menu->menu_stack);

      /* Avoid new binds triggering things right away. */
      menu->trigger_state = 0;
      menu->old_input_state = -1ULL;

      /* We won't be getting any key events, so just cancel early. */
      if (timed_out)
         input_keyboard_wait_keys_cancel();
   }

   return 0;
}


static int menu_action_ok(const char *menu_path,
      const char *menu_label, unsigned menu_type)
{
   const char *label = NULL;
   const char *path = NULL;
   unsigned type = 0;
   rarch_setting_t *setting_data = (rarch_setting_t *)
      setting_data_get_list();
   rarch_setting_t *setting = (rarch_setting_t*)
      setting_data_find_setting(setting_data, menu_label);

   if (file_list_get_size(driver.menu->selection_buf) == 0)
      return 0;

   file_list_get_at_offset(driver.menu->selection_buf,
         driver.menu->selection_ptr, &path, &label, &type);

#if 0
   RARCH_LOG("menu label: %s\n", menu_label);
   RARCH_LOG("type     : %d\n", type == MENU_FILE_USE_DIRECTORY);
   RARCH_LOG("type id  : %d\n", type);
#endif

   if (type == MENU_FILE_PLAYLIST_ENTRY)
   {
      rarch_playlist_load_content(g_extern.history,
            driver.menu->selection_ptr);
      menu_flush_stack_type(driver.menu->menu_stack, MENU_SETTINGS);
      return -1;
   }
   else if (!strcmp(menu_label, "detect_core_list")
         && type == MENU_FILE_PLAIN)
   {
      int ret = rarch_defer_core(g_extern.core_info,
            menu_path, path, driver.menu->deferred_path,
            sizeof(driver.menu->deferred_path));

      if (ret == -1)
      {

         rarch_main_command(RARCH_CMD_LOAD_CONTENT);
         rarch_main_command(RARCH_CMD_LOAD_CORE);
         menu_flush_stack_type(driver.menu->menu_stack,MENU_SETTINGS);
         driver.menu->msg_force = true;
         return -1;
      }
      else if (ret == 0)
         menu_entries_push(driver.menu->menu_stack,
               g_settings.libretro_directory, "deferred_core_list",
               0, driver.menu->selection_ptr);
   }
   else if ((setting && setting->type == ST_DIR)
         && (type == MENU_FILE_USE_DIRECTORY))
   {
      menu_common_setting_set_current_string(setting, menu_path);
      menu_entries_pop_stack(driver.menu->menu_stack, setting->name);
   }
   else if ((setting && setting->type == ST_PATH)
         && (type == MENU_FILE_PLAIN))
   {
      menu_common_setting_set_current_string_path(setting, menu_path, path);
      menu_entries_pop_stack(driver.menu->menu_stack, setting->name);
   }
#ifdef HAVE_SHADER_MANAGER
   else if (!strcmp(menu_label, "video_shader_preset")
         && type == MENU_FILE_PLAIN)
   {
      char shader_path[PATH_MAX];
      fill_pathname_join(shader_path, menu_path, path, sizeof(shader_path));
      if (driver.menu_ctx && driver.menu_ctx->backend &&
            driver.menu_ctx->backend->shader_manager_set_preset)
         driver.menu_ctx->backend->shader_manager_set_preset(
               driver.menu->shader,
               gfx_shader_parse_type(shader_path, RARCH_SHADER_NONE),
               shader_path);
      menu_flush_stack_label(driver.menu->menu_stack, "Shader Options");
   }
   else if (!strcmp(menu_label, "video_shader_pass")
         && type == MENU_FILE_PLAIN)
   {
      fill_pathname_join(driver.menu->shader->pass[hack_shader_pass].source.path,
            menu_path, path,
            sizeof(driver.menu->shader->pass[hack_shader_pass].source.path));

      /* This will reset any changed parameters. */
      gfx_shader_resolve_parameters(NULL, driver.menu->shader);
      menu_flush_stack_label(driver.menu->menu_stack, "Shader Options");
   }
#endif
   else if (!strcmp(menu_label, "deferred_core_list")
         && type == MENU_FILE_CORE)
   {
      strlcpy(g_settings.libretro, path, sizeof(g_settings.libretro));
      strlcpy(g_extern.fullpath, driver.menu->deferred_path,
            sizeof(g_extern.fullpath));
      rarch_main_command(RARCH_CMD_LOAD_CONTENT);
      driver.menu->msg_force = true;
      menu_flush_stack_type(driver.menu->menu_stack,MENU_SETTINGS);
      return -1;
   }
   else if (!strcmp(menu_label, "core_list")
         && type == MENU_FILE_CORE)
   {
      fill_pathname_join(g_settings.libretro, menu_path, path,
            sizeof(g_settings.libretro));
      rarch_main_command(RARCH_CMD_LOAD_CORE);
      menu_flush_stack_type(driver.menu->menu_stack,MENU_SETTINGS);
#if defined(HAVE_DYNAMIC)
      /* No content needed for this core, load core immediately. */
      if (driver.menu->load_no_content)
      {
         rarch_main_set_state(RARCH_ACTION_STATE_LOAD_CONTENT);
         *g_extern.fullpath = '\0';
         driver.menu->msg_force = true;
         return -1;
      }

      /* Core selection on non-console just updates directory listing.
       * Will take effect on new content load. */
#elif defined(RARCH_CONSOLE)
      rarch_main_command(RARCH_CMD_RESTART_RETROARCH);
      return -1;
#endif
   }
   else if (!strcmp(menu_label, "configurations")
         && type == MENU_FILE_PLAIN)
   {
      char config[PATH_MAX];
      fill_pathname_join(config, menu_path, path, sizeof(config));
      menu_flush_stack_type(driver.menu->menu_stack,MENU_SETTINGS);
      driver.menu->msg_force = true;
      if (rarch_replace_config(config))
      {
         menu_clear_navigation(driver.menu);
         return -1;
      }
   }
   else if (!strcmp(menu_label, "disk_image_append")
         && type == MENU_FILE_PLAIN)
   {
      char image[PATH_MAX];
      fill_pathname_join(image, menu_path, path, sizeof(image));
      rarch_disk_control_append_image(image);

      rarch_main_command(RARCH_CMD_RESUME);

      menu_flush_stack_type(driver.menu->menu_stack,MENU_SETTINGS);
      return -1;
   }
   else if (menu_parse_check(label, type) == 0)
   {
      char cat_path[PATH_MAX];
      fill_pathname_join(cat_path, menu_path, path, sizeof(cat_path));

      menu_entries_push(driver.menu->menu_stack,
            cat_path, menu_label, type, driver.menu->selection_ptr);
   }
   else if (type == MENU_FILE_CARCHIVE)
   {
      char cat_path[PATH_MAX];
      fill_pathname_join(cat_path, menu_path, path, sizeof(cat_path));

      menu_entries_push(driver.menu->menu_stack,
            cat_path, menu_label, type, driver.menu->selection_ptr);
      return 0;
   }
#ifdef HAVE_COMPRESSION
   else if (type == MENU_FILE_IN_CARCHIVE)
   {
      fill_pathname_join(g_extern.fullpath, menu_path, path,
            sizeof(g_extern.fullpath));

      g_extern.is_carchive = true;
      strncpy(g_extern.carchive_path, menu_path, 
            sizeof(g_extern.carchive_path));

      rarch_main_set_state(RARCH_ACTION_STATE_LOAD_CONTENT);
      menu_flush_stack_type(driver.menu->menu_stack,MENU_SETTINGS);
      driver.menu->msg_force = true;
      return -1;
   }
#endif
   else
   {
      fill_pathname_join(g_extern.fullpath, menu_path, path,
            sizeof(g_extern.fullpath));
      rarch_main_set_state(RARCH_ACTION_STATE_LOAD_CONTENT);

      menu_flush_stack_type(driver.menu->menu_stack,MENU_SETTINGS);
      driver.menu->msg_force = true;
      return -1;
   }

   return 0;
}

static int menu_common_iterate(unsigned action)
{
   int ret = 0;
   unsigned menu_type = 0;
   const char *path = NULL;
   const char *menu_label = NULL;

   file_list_get_last(driver.menu->menu_stack, &path, &menu_label, &menu_type);

   if (driver.video_data && driver.menu_ctx && driver.menu_ctx->set_texture)
      driver.menu_ctx->set_texture(driver.menu);

   if (!strcmp(menu_label, "help"))
      return menu_start_screen_iterate(action);
   else if (!strcmp(menu_label, "info_screen"))
      return menu_info_screen_iterate(action);
   else if (menu_common_type_is(menu_label, menu_type) == MENU_SETTINGS)
      return menu_settings_iterate(action);
   else if (
         menu_type == MENU_SETTINGS_CUSTOM_VIEWPORT ||
         !strcmp(menu_label, "custom_viewport_2")
         )
      return menu_viewport_iterate(action);
   else if (
         menu_type == MENU_SETTINGS_CUSTOM_BIND ||
         menu_type == MENU_SETTINGS_CUSTOM_BIND_KEYBOARD
         )
   {
      char msg[PATH_MAX];
      int64_t current;
      int timeout = 0;

      if (driver.video_data && driver.menu_ctx &&
            driver.menu_ctx->render)
         driver.menu_ctx->render();

      if (menu_type == MENU_SETTINGS_CUSTOM_BIND)
      {
         snprintf(msg, sizeof(msg), "[%s]\npress joypad\n(RETURN to skip)",
               input_config_bind_map[
               driver.menu->binds.begin - MENU_SETTINGS_BIND_BEGIN].desc);
      }
      else if (menu_type == MENU_SETTINGS_CUSTOM_BIND_KEYBOARD)
      {
         current = rarch_get_time_usec();
         timeout = (driver.menu->binds.timeout_end - current) / 1000000;
         snprintf(msg, sizeof(msg), "[%s]\npress keyboard\n(timeout %d seconds)",
               input_config_bind_map[
               driver.menu->binds.begin - MENU_SETTINGS_BIND_BEGIN].desc,
               timeout);
      }

      if (driver.video_data && driver.menu_ctx 
            && driver.menu_ctx->render_messagebox)
         driver.menu_ctx->render_messagebox(msg);

      if (menu_type == MENU_SETTINGS_CUSTOM_BIND)
         return menu_custom_bind_iterate(driver.menu);
      else if (menu_type == MENU_SETTINGS_CUSTOM_BIND_KEYBOARD)
         return menu_custom_bind_iterate_keyboard(driver.menu, current, timeout);
   }

   if (driver.menu->need_refresh && action != MENU_ACTION_MESSAGE)
      action = MENU_ACTION_NOOP;

   unsigned scroll_speed = (max(driver.menu->scroll_accel, 2) - 2) / 4 + 1;
   unsigned fast_scroll_speed = 4 + 4 * scroll_speed;

   switch (action)
   {
      case MENU_ACTION_UP:
         if (driver.menu->selection_ptr >= scroll_speed)
            menu_set_navigation(driver.menu,
                  driver.menu->selection_ptr - scroll_speed);
         else
            menu_set_navigation(driver.menu,
                  file_list_get_size(driver.menu->selection_buf) - 1);
         break;

      case MENU_ACTION_DOWN:
         if (driver.menu->selection_ptr + scroll_speed <
               file_list_get_size(driver.menu->selection_buf))
            menu_set_navigation(driver.menu,
                  driver.menu->selection_ptr + scroll_speed);
         else
            menu_clear_navigation(driver.menu);
         break;

      case MENU_ACTION_LEFT:
         if (driver.menu->selection_ptr > fast_scroll_speed)
            menu_set_navigation(driver.menu,
                  driver.menu->selection_ptr - fast_scroll_speed);
         else
            menu_clear_navigation(driver.menu);
         break;

      case MENU_ACTION_RIGHT:
         if (driver.menu->selection_ptr + fast_scroll_speed <
               file_list_get_size(driver.menu->selection_buf))
            menu_set_navigation(driver.menu,
                  driver.menu->selection_ptr + fast_scroll_speed);
         else
            menu_set_navigation_last(driver.menu);
         break;

      case MENU_ACTION_SCROLL_UP:
         menu_descend_alphabet(driver.menu, &driver.menu->selection_ptr);
         break;
      case MENU_ACTION_SCROLL_DOWN:
         menu_ascend_alphabet(driver.menu, &driver.menu->selection_ptr);
         break;

      case MENU_ACTION_CANCEL:
         menu_entries_pop(driver.menu->menu_stack);
         break;

      case MENU_ACTION_OK:
         ret = menu_action_ok(path, menu_label, menu_type);
         break;

      case MENU_ACTION_REFRESH:
         menu_clear_navigation(driver.menu);
         driver.menu->need_refresh = true;
         break;

      case MENU_ACTION_MESSAGE:
         driver.menu->msg_force = true;
         break;

      default:
         break;
   }

   if (driver.menu->need_refresh)
   {
      if (menu_parse_and_resolve(driver.menu->selection_buf,
               driver.menu->menu_stack) == 0)
         driver.menu->need_refresh = false;
   }

   if (driver.menu_ctx && driver.menu_ctx->iterate)
      driver.menu_ctx->iterate(driver.menu, action);

   if (driver.video_data && driver.menu_ctx && driver.menu_ctx->render)
      driver.menu_ctx->render();

   return ret;
}

static void menu_common_setting_set_label(char *type_str,
      size_t type_str_size, unsigned *w, unsigned type, 
      const char *menu_label, const char *label, unsigned index)
{
   setting_data_get_label(type_str, type_str_size, w, 
         type, menu_label, label, index);
}

const menu_ctx_driver_backend_t menu_ctx_backend_common = {
   menu_common_iterate,
#ifdef HAVE_SHADER_MANAGER
   menu_common_shader_manager_init,
   menu_common_shader_manager_get_str,
   menu_common_shader_manager_set_preset,
   menu_common_shader_manager_save_preset,
   menu_common_shader_manager_get_type,
   menu_common_shader_manager_setting_toggle,
#else
   NULL,
   NULL,
   NULL,
   NULL,
   NULL,
   NULL,
#endif
   menu_common_type_is,
   menu_common_setting_set_label,
   "menu_common",
};
