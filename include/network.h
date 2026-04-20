#ifndef NETWORK_H
#define NETWORK_H

#include <td/telegram/td_json_client.h>
#include <cJSON.h>
#include "config.h"

void *td_client_create(void);
void td_client_send(void *client, cJSON *request);
cJSON *td_client_execute(cJSON *request);
cJSON *td_client_receive(void *client, double timeout);
void td_client_destroy(void *client);
void handle_auth(void *client, Config *cfg, cJSON *update);

#endif
