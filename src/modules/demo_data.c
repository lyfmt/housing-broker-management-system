#include "demo_data.h"

#include "rental_system.h"

static const char *const kRegions[] = {"浑南区", "和平区", "皇姑区"};
static const char *const kFloorNotes[] = {"低层", "中层", "高层"};
static const char *const kOrientations[] = {"南", "南北", "东西"};
static const char *const kHouseTypes[] = {"一室一卫", "二室一厅一卫", "三室一厅两卫"};
static const char *const kDecorations[] = {"简装", "精装"};

static const DemoAgentEntry kAgents[] = {
    {1001, "张中介", "男", "13800000001", "210102198803153216", "123456"},
    {1002, "李楠", "女", "13800000002", "210103198907214536", "123456"},
    {1003, "王凯", "男", "13800000003", "210104199001126258", "123456"},
    {1004, "许大飞", "男", "13700000001", "210105198612083519", "123456"},
    {1005, "马超", "男", "13700000005", "210106198901016257", "123456"},
    {1006, "司马诸葛", "男", "13700000006", "210107199002146611", "123456"},
    {1007, "诸葛青", "男", "13700000007", "210108199310213634", "123456"},
    {1008, "马岚", "女", "13700000008", "210109199504287146", "123456"},
    {1009, "司马云", "女", "13700000009", "210110199612036083", "123456"},
    {1010, "赵云飞", "男", "13700000010", "210111198711058575", "123456"},
};

static const DemoTenantEntry kTenants[] = {
    {5001, "赵敏", "女", "13911112222", "210102199512123628", "tenant01"},
    {5002, "钱浩", "男", "13911113333", "210103199804053219", "tenant02"},
    {5003, "孙悦", "女", "13911114444", "21010419990316842X", "tenant03"},
    {5004, "马超", "男", "13911115555", "210105199404179531", "tenant04"},
    {5005, "司马诸葛", "男", "13911116666", "210106199601225237", "tenant05"},
    {5006, "马小雨", "女", "13911117777", "210107199710116428", "tenant06"},
    {5007, "诸葛亮亮", "男", "13911118888", "210108199811092736", "tenant07"},
    {5008, "司马昭", "男", "13911119999", "210109199912257214", "tenant08"},
    {5009, "黄月英", "女", "13911110001", "210110199211306122", "tenant09"},
    {5010, "马腾", "男", "13911110002", "210111198812018451", "tenant10"},
};

static const DemoHouseEntry kHouses[] = {
    {2001, "沈阳", "浑南区", "华润悦府", "浑南中路88号", "1栋", 12, "1202室", "中层", "南北", "二室一厅一卫", 89.5, "精装", 3200, "刘先生", "13788889999", 1001, HOUSE_VACANT},
    {2002, "沈阳", "和平区", "万科春河里", "南京南街26号", "2栋", 8, "803室", "中层", "南", "一室一卫", 52.0, "简装", 2100, "周女士", "13677778888", 1002, HOUSE_VACANT},
    {2003, "沈阳", "皇姑区", "保利海上五月花", "崇山路66号", "6栋", 15, "1501室", "高层", "南北", "三室一厅两卫", 128.0, "精装", 4500, "吴先生", "13566667777", 1003, HOUSE_RENTED},
    {2004, "沈阳", "和平区", "幸福小区", "中华路128号", "3栋", 10, "1002室", "中层", "南北", "二室一厅一卫", 92.0, "精装", 4100, "陈先生", "13612345671", 1005, HOUSE_VACANT},
    {2005, "沈阳", "浑南区", "幸福家园", "浑南东路56号", "5栋", 14, "1401室", "高层", "南", "二室一厅一卫", 88.0, "精装", 3950, "杨女士", "13612345672", 1006, HOUSE_VACANT},
    {2006, "沈阳", "皇姑区", "幸福里", "崇山中路30号", "2栋", 9, "902室", "中层", "南北", "二室一厅一卫", 95.0, "简装", 4050, "刘女士", "13612345673", 1007, HOUSE_VACANT},
    {2007, "沈阳", "和平区", "幸福雅苑", "太原街88号", "1栋", 11, "1103室", "中层", "南", "二室一厅一卫", 86.5, "精装", 4000, "宋先生", "13612345674", 1008, HOUSE_VACANT},
    {2008, "沈阳", "浑南区", "幸福新城", "创新路66号", "8栋", 16, "1602室", "高层", "南北", "三室一厅两卫", 102.0, "精装", 4300, "周先生", "13612345675", 1009, HOUSE_VACANT},
    {2009, "沈阳", "皇姑区", "幸福花园", "黄河南大街99号", "4栋", 7, "702室", "中层", "南", "二室一厅一卫", 90.0, "简装", 3900, "吴女士", "13612345676", 1010, HOUSE_VACANT},
};

static const DemoViewingEntry kViewings[] = {
    {3001, "2026-03-20 14:30", 2001, 5001, 1002, 45, VIEWING_CONFIRMED, VIEWING_CONTRACT_NONE, "-", "已电话确认"},
    {3002, "2026-03-22 10:00", 2002, 5002, 0, 30, VIEWING_UNCONFIRMED, VIEWING_CONTRACT_NONE, "-", "-"},
};

static const DemoRentalEntry kRentals[] = {
    {4001, 2003, 5003, 1001, 0, "2026-02-15", "2026-03-01", "2027-02-28", 12, 4300, 8600, "", "", RENTAL_ACTIVE, RENTAL_SIGN_CONFIRMED},
};

const char *const *demo_regions(size_t *count) {
    if (count) *count = sizeof(kRegions) / sizeof(kRegions[0]);
    return kRegions;
}

const char *const *demo_floor_notes(size_t *count) {
    if (count) *count = sizeof(kFloorNotes) / sizeof(kFloorNotes[0]);
    return kFloorNotes;
}

const char *const *demo_orientations(size_t *count) {
    if (count) *count = sizeof(kOrientations) / sizeof(kOrientations[0]);
    return kOrientations;
}

const char *const *demo_house_types(size_t *count) {
    if (count) *count = sizeof(kHouseTypes) / sizeof(kHouseTypes[0]);
    return kHouseTypes;
}

const char *const *demo_decorations(size_t *count) {
    if (count) *count = sizeof(kDecorations) / sizeof(kDecorations[0]);
    return kDecorations;
}

const DemoAgentEntry *demo_agent_entries(size_t *count) {
    if (count) *count = sizeof(kAgents) / sizeof(kAgents[0]);
    return kAgents;
}

const DemoTenantEntry *demo_tenant_entries(size_t *count) {
    if (count) *count = sizeof(kTenants) / sizeof(kTenants[0]);
    return kTenants;
}

const DemoHouseEntry *demo_house_entries(size_t *count) {
    if (count) *count = sizeof(kHouses) / sizeof(kHouses[0]);
    return kHouses;
}

const DemoViewingEntry *demo_viewing_entries(size_t *count) {
    if (count) *count = sizeof(kViewings) / sizeof(kViewings[0]);
    return kViewings;
}

const DemoRentalEntry *demo_rental_entries(size_t *count) {
    if (count) *count = sizeof(kRentals) / sizeof(kRentals[0]);
    return kRentals;
}
