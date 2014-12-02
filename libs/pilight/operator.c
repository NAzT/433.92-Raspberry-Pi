/*
	Copyright (C) 2013 - 2014 CurlyMo

	This file is part of pilight.

	pilight is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	pilight is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <libgen.h>
#include <dirent.h>
#include <dlfcn.h>

#include "../../pilight.h"
#include "common.h"
#include "settings.h"
#include "dso.h"
#include "log.h"

#include "operator.h"
#include "operator_header.h"

void event_operator_remove(char *name) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

	struct event_operators_t *currP, *prevP;

	prevP = NULL;

	for(currP = event_operators; currP != NULL; prevP = currP, currP = currP->next) {

		if(strcmp(currP->name, name) == 0) {
			if(prevP == NULL) {
				event_operators = currP->next;
			} else {
				prevP->next = currP->next;
			}

			logprintf(LOG_DEBUG, "removed operator %s", currP->name);
			sfree((void *)&currP->name);
			sfree((void *)&currP);

			break;
		}
	}
}

void event_operator_init(void) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

	#include "operator_init.h"
	void *handle = NULL;
	void (*init)(void);
	void (*compatibility)(struct module_t *module);
	char path[255];
	struct module_t module;
	char pilight_version[strlen(VERSION)+1];
	char pilight_commit[3];
	char *operator_root = NULL;
	int check1 = 0, check2 = 0, valid = 1, operator_root_free = 0;
	strcpy(pilight_version, VERSION);

	struct dirent *file = NULL;
	DIR *d = NULL;

	memset(pilight_commit, '\0', 3);

	if(settings_find_string("operator-root", &operator_root) != 0) {
		/* If no operator root was set, use the default operator root */
		if(!(operator_root = malloc(strlen(OPERATOR_ROOT)+1))) {
			logprintf(LOG_ERR, "out of memory");
			exit(EXIT_FAILURE);
		}
		strcpy(operator_root, OPERATOR_ROOT);
		operator_root_free = 1;
	}
	size_t len = strlen(operator_root);
	if(operator_root[len-1] != '/') {
		strcat(operator_root, "/");
	}

	if((d = opendir(operator_root))) {
		while((file = readdir(d)) != NULL) {
			if(file->d_type == DT_REG) {
				if(strstr(file->d_name, ".so") != NULL) {
					valid = 1;
					memset(path, '\0', 255);
					sprintf(path, "%s%s", operator_root, file->d_name);

					if((handle = dso_load(path))) {
						init = dso_function(handle, "init");
						compatibility = dso_function(handle, "compatibility");
						if(init && compatibility) {
							compatibility(&module);
							if(module.name && module.version && module.reqversion) {
								char ver[strlen(module.reqversion)+1];
								strcpy(ver, module.reqversion);

								if((check1 = vercmp(ver, pilight_version)) > 0) {
									valid = 0;
								}

								if(check1 == 0 && module.reqcommit) {
									char com[strlen(module.reqcommit)+1];
									strcpy(com, module.reqcommit);
									sscanf(HASH, "v%*[0-9].%*[0-9]-%[0-9]-%*[0-9a-zA-Z\n\r]", pilight_commit);

									if(strlen(pilight_commit) > 0 && (check2 = vercmp(com, pilight_commit)) > 0) {
										valid = 0;
									}
								}
								if(valid) {
									char tmp[strlen(module.name)+1];
									strcpy(tmp, module.name);
									event_operator_remove(tmp);
									init();
									logprintf(LOG_DEBUG, "loaded operator %s", file->d_name);
								} else {
									if(module.reqcommit) {
										logprintf(LOG_ERR, "event operator %s requires at least pilight v%s (commit %s)", file->d_name, module.reqversion, module.reqcommit);
									} else {
										logprintf(LOG_ERR, "event operator %s requires at least pilight v%s", file->d_name, module.reqversion);
									}
								}
							} else {
								logprintf(LOG_ERR, "invalid module %s", file->d_name);
							}
						}
					}
				}
			}
		}
		closedir(d);
	}
	if(operator_root_free) {
		sfree((void *)&operator_root);
	}
}

void event_operator_register(struct event_operators_t **op, const char *name) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

	if(!(*op = malloc(sizeof(struct event_operators_t)))) {
		logprintf(LOG_ERR, "out of memory");
		exit(EXIT_FAILURE);
	}
	if(!((*op)->name = malloc(strlen(name)+1))) {
		logprintf(LOG_ERR, "out of memory");
		exit(EXIT_FAILURE);
	}
	strcpy((*op)->name, name);

	(*op)->callback_string = NULL;
	(*op)->callback_number = NULL;

	(*op)->next = event_operators;
	event_operators = (*op);
}

int event_operator_gc(void) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

	struct event_operators_t *tmp_operator = NULL;
	while(event_operators) {
		tmp_operator = event_operators;
		sfree((void *)&tmp_operator->name);
		event_operators = event_operators->next;
		sfree((void *)&tmp_operator);
	}
	sfree((void *)&event_operators);
	logprintf(LOG_DEBUG, "garbage collected event operator library");
	return 0;
}
