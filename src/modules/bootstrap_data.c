/*
 * 文件: bootstrap_data.c
 * 定义内容: 默认字典与演示数据装载/补齐逻辑，负责把静态演示表写入运行期数据库。
 * 后续用途: 保障首跑可演示性，并作为回归测试时快速恢复基准数据的入口。
 */
#include "bootstrap_data.h"

#include <stdlib.h>
#include <string.h>

#include "demo_data.h"

static void safe_copy(char *dst, size_t dstSize, const char *src) {
    if (!dst || dstSize == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static void category_add_default(CategoryList *list, const char *value) {
    int i;
    if (!list || !value) return;
    if (list->count >= MAX_CATEGORY_ITEMS) return;
    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i], value) == 0) return;
    }
    safe_copy(list->items[list->count], MAX_STR, value);
    list->count++;
}

static AgentNode *db_find_agent(Database *db, int id) {
    AgentNode *cur;
    if (!db) return NULL;
    for (cur = db->agents; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static TenantNode *db_find_tenant(Database *db, int id) {
    TenantNode *cur;
    if (!db) return NULL;
    for (cur = db->tenants; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static HouseNode *db_find_house(Database *db, int id) {
    HouseNode *cur;
    if (!db) return NULL;
    for (cur = db->houses; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static ViewingNode *db_find_viewing(Database *db, int id) {
    ViewingNode *cur;
    if (!db) return NULL;
    for (cur = db->viewings; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static RentalNode *db_find_rental(Database *db, int id) {
    RentalNode *cur;
    if (!db) return NULL;
    for (cur = db->rentals; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static int db_append_agent(Database *db, const Agent *a) {
    AgentNode *node;
    AgentNode **tail;
    if (!db || !a) return 0;
    node = (AgentNode *)malloc(sizeof(*node));
    if (!node) return 0;
    node->data = *a;
    node->next = NULL;
    tail = &db->agents;
    while (*tail) tail = &(*tail)->next;
    *tail = node;
    db->agentCount++;
    return 1;
}

static int db_append_tenant(Database *db, const Tenant *t) {
    TenantNode *node;
    TenantNode **tail;
    if (!db || !t) return 0;
    node = (TenantNode *)malloc(sizeof(*node));
    if (!node) return 0;
    node->data = *t;
    node->next = NULL;
    tail = &db->tenants;
    while (*tail) tail = &(*tail)->next;
    *tail = node;
    db->tenantCount++;
    return 1;
}

static int db_append_house(Database *db, const House *h) {
    HouseNode *node;
    HouseNode **tail;
    if (!db || !h) return 0;
    node = (HouseNode *)malloc(sizeof(*node));
    if (!node) return 0;
    node->data = *h;
    node->next = NULL;
    tail = &db->houses;
    while (*tail) tail = &(*tail)->next;
    *tail = node;
    db->houseCount++;
    return 1;
}

static int db_append_viewing(Database *db, const Viewing *v) {
    ViewingNode *node;
    ViewingNode **tail;
    if (!db || !v) return 0;
    node = (ViewingNode *)malloc(sizeof(*node));
    if (!node) return 0;
    node->data = *v;
    node->next = NULL;
    tail = &db->viewings;
    while (*tail) tail = &(*tail)->next;
    *tail = node;
    db->viewingCount++;
    return 1;
}

static int db_append_rental(Database *db, const Rental *r) {
    RentalNode *node;
    RentalNode **tail;
    if (!db || !r) return 0;
    node = (RentalNode *)malloc(sizeof(*node));
    if (!node) return 0;
    node->data = *r;
    node->next = NULL;
    tail = &db->rentals;
    while (*tail) tail = &(*tail)->next;
    *tail = node;
    db->rentalCount++;
    return 1;
}

void bootstrap_init_defaults(Database *db) {
    size_t i, count;
    const char *const *items;
    const DemoAgentEntry *agents;
    Agent a;

    if (!db) return;

    memset(db, 0, sizeof(*db));
    safe_copy(db->adminPassword, sizeof(db->adminPassword), "admin123");

    items = demo_regions(&count);
    for (i = 0; i < count; ++i) category_add_default(&db->regions, items[i]);
    items = demo_floor_notes(&count);
    for (i = 0; i < count; ++i) category_add_default(&db->floorNotes, items[i]);
    items = demo_orientations(&count);
    for (i = 0; i < count; ++i) category_add_default(&db->orientations, items[i]);
    items = demo_house_types(&count);
    for (i = 0; i < count; ++i) category_add_default(&db->houseTypes, items[i]);
    items = demo_decorations(&count);
    for (i = 0; i < count; ++i) category_add_default(&db->decorations, items[i]);

    agents = demo_agent_entries(&count);
    if (count > 0) {
        memset(&a, 0, sizeof(a));
        a.id = agents[0].id;
        safe_copy(a.name, sizeof(a.name), agents[0].name);
        safe_copy(a.gender, sizeof(a.gender), agents[0].gender);
        safe_copy(a.phone, sizeof(a.phone), agents[0].phone);
        safe_copy(a.idCard, sizeof(a.idCard), agents[0].idCard);
        safe_copy(a.password, sizeof(a.password), agents[0].password[0] ? agents[0].password : DEFAULT_AGENT_PASSWORD);
        db_append_agent(db, &a);
    }
}

static void seed_agent_entry(Database *db, const DemoAgentEntry *entry) {
    Agent a;
    if (!db || !entry || db_find_agent(db, entry->id)) return;
    memset(&a, 0, sizeof(a));
    a.id = entry->id;
    safe_copy(a.name, sizeof(a.name), entry->name);
    safe_copy(a.gender, sizeof(a.gender), entry->gender);
    safe_copy(a.phone, sizeof(a.phone), entry->phone);
    safe_copy(a.idCard, sizeof(a.idCard), entry->idCard);
    safe_copy(a.password, sizeof(a.password), entry->password && entry->password[0] ? entry->password : DEFAULT_AGENT_PASSWORD);
    db_append_agent(db, &a);
}

static void seed_tenant_entry(Database *db, const DemoTenantEntry *entry) {
    Tenant t;
    if (!db || !entry || db_find_tenant(db, entry->id)) return;
    memset(&t, 0, sizeof(t));
    t.id = entry->id;
    safe_copy(t.name, sizeof(t.name), entry->name);
    safe_copy(t.gender, sizeof(t.gender), entry->gender);
    safe_copy(t.phone, sizeof(t.phone), entry->phone);
    safe_copy(t.idCard, sizeof(t.idCard), entry->idCard);
    safe_copy(t.password, sizeof(t.password), entry->password && entry->password[0] ? entry->password : DEFAULT_TENANT_PASSWORD);
    db_append_tenant(db, &t);
}

static void seed_house_entry(Database *db, const DemoHouseEntry *entry) {
    House h;
    if (!db || !entry || db_find_house(db, entry->id)) return;
    memset(&h, 0, sizeof(h));
    h.id = entry->id;
    safe_copy(h.city, sizeof(h.city), entry->city);
    safe_copy(h.region, sizeof(h.region), entry->region);
    safe_copy(h.community, sizeof(h.community), entry->community);
    safe_copy(h.address, sizeof(h.address), entry->address);
    safe_copy(h.building, sizeof(h.building), entry->building);
    h.floor = entry->floor;
    safe_copy(h.unitNo, sizeof(h.unitNo), entry->unitNo);
    safe_copy(h.floorNote, sizeof(h.floorNote), entry->floorNote);
    safe_copy(h.orientation, sizeof(h.orientation), entry->orientation);
    safe_copy(h.houseType, sizeof(h.houseType), entry->houseType);
    h.area = entry->area;
    safe_copy(h.decoration, sizeof(h.decoration), entry->decoration);
    h.price = entry->price;
    safe_copy(h.ownerName, sizeof(h.ownerName), entry->ownerName);
    safe_copy(h.ownerPhone, sizeof(h.ownerPhone), entry->ownerPhone);
    h.createdByAgentId = entry->createdByAgentId;
    h.status = entry->status;
    h.rejectReason[0] = '\0';
    db_append_house(db, &h);
}

static void seed_viewing_entry(Database *db, const DemoViewingEntry *entry) {
    Viewing v;
    if (!db || !entry || db_find_viewing(db, entry->id)) return;
    memset(&v, 0, sizeof(v));
    v.id = entry->id;
    safe_copy(v.datetime, sizeof(v.datetime), entry->datetime);
    v.houseId = entry->houseId;
    v.tenantId = entry->tenantId;
    v.agentId = entry->agentId;
    v.durationMinutes = entry->durationMinutes;
    v.status = entry->status;
    v.contractStatus = entry->contractStatus;
    safe_copy(v.tenantFeedback, sizeof(v.tenantFeedback), entry->tenantFeedback);
    safe_copy(v.agentFeedback, sizeof(v.agentFeedback), entry->agentFeedback);
    db_append_viewing(db, &v);
}

static void seed_rental_entry(Database *db, const DemoRentalEntry *entry) {
    Rental r;
    if (!db || !entry || db_find_rental(db, entry->id)) return;
    memset(&r, 0, sizeof(r));
    r.id = entry->id;
    r.houseId = entry->houseId;
    r.tenantId = entry->tenantId;
    r.agentId = entry->agentId;
    r.appointmentId = entry->appointmentId;
    safe_copy(r.contractDate, sizeof(r.contractDate), entry->contractDate);
    safe_copy(r.startDate, sizeof(r.startDate), entry->startDate);
    safe_copy(r.endDate, sizeof(r.endDate), entry->endDate);
    r.leaseTerm = entry->leaseTerm;
    r.monthlyRent = entry->monthlyRent;
    r.deposit = entry->deposit;
    safe_copy(r.otherTerms, sizeof(r.otherTerms), entry->otherTerms);
    safe_copy(r.rejectReason, sizeof(r.rejectReason), entry->rejectReason);
    r.status = entry->status;
    r.signStatus = entry->signStatus;
    db_append_rental(db, &r);
}

void bootstrap_seed_demo_data(Database *db) {
    size_t i, count;
    const DemoAgentEntry *agents;
    const DemoTenantEntry *tenants;
    const DemoHouseEntry *houses;
    const DemoViewingEntry *viewings;
    const DemoRentalEntry *rentals;

    if (!db) return;

    agents = demo_agent_entries(&count);
    for (i = 0; i < count; ++i) seed_agent_entry(db, &agents[i]);

    tenants = demo_tenant_entries(&count);
    for (i = 0; i < count; ++i) seed_tenant_entry(db, &tenants[i]);

    houses = demo_house_entries(&count);
    for (i = 0; i < count; ++i) seed_house_entry(db, &houses[i]);

    viewings = demo_viewing_entries(&count);
    for (i = 0; i < count; ++i) seed_viewing_entry(db, &viewings[i]);

    rentals = demo_rental_entries(&count);
    for (i = 0; i < count; ++i) seed_rental_entry(db, &rentals[i]);
}

int bootstrap_upgrade_demo_agent_id_cards(Database *db) {
    const DemoAgentEntry *defaults;
    size_t count;
    size_t i;
    int changed = 0;

    if (!db) return 0;

    defaults = demo_agent_entries(&count);
    for (i = 0; i < count; ++i) {
        AgentNode *a = db_find_agent(db, defaults[i].id);
        if (!a || a->data.idCard[0]) continue;
        safe_copy(a->data.idCard, sizeof(a->data.idCard), defaults[i].idCard);
        changed = 1;
    }
    return changed;
}
