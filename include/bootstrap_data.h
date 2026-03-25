#ifndef BOOTSTRAP_DATA_H
#define BOOTSTRAP_DATA_H

#include "rental_system.h"

void bootstrap_init_defaults(Database *db);
void bootstrap_seed_demo_data(Database *db);
int bootstrap_upgrade_demo_agent_id_cards(Database *db);

#endif
