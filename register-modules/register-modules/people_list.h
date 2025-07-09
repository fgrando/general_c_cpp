#pragma once

#include "people.h"

void people_list_init();

Person* people_list();
int people_list_count();
int add_person(Person new_person);
