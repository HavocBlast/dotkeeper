/*
 * config.h: per-user settings in $XDG_CONFIG_HOME/dotkeeper/config.ini.
 * Today that is only the location of the dotfile repo.
 */
#ifndef DK_CONFIG_H
#define DK_CONFIG_H

#include "../util/util.h"

char *dk_config_path(void);
/* Sets *repo to the configured repo path, or NULL if none is set. */
int dk_config_load(char **repo, dk_err *err);
int dk_config_save(const char *repo, dk_err *err);

#endif
