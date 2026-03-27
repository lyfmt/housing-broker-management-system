/*
 * 文件: demo_data.c
 * 定义内容: 演示用静态数据仓（中介/租客/房源/看房/租约）与只读访问接口。
 * 后续用途: 支撑课程答辩演示、功能联调和统计口径验证。
 */
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
    {5011, "林晨", "男", "13911110003", "210112199203158779", "tenant11"},
};

static const DemoHouseEntry kHouses[] = {
    {2001, "沈阳", "浑南区", "华润悦府", "浑南中路88号", "1栋", 12, "1202室", "中层", "南北", "二室一厅一卫", 89.5, "精装", 3200, "刘先生", "13788889999", 1001, HOUSE_RENTED},
    {2002, "沈阳", "和平区", "万科春河里", "南京南街26号", "2栋", 8, "803室", "中层", "南", "一室一卫", 52.0, "简装", 2100, "周女士", "13677778888", 1002, HOUSE_RENTED},
    {2003, "沈阳", "皇姑区", "保利海上五月花", "崇山路66号", "6栋", 15, "1501室", "高层", "南北", "三室一厅两卫", 128.0, "精装", 4500, "吴先生", "13566667777", 1003, HOUSE_RENTED},
    {2004, "沈阳", "和平区", "幸福小区", "中华路128号", "3栋", 10, "1002室", "中层", "南北", "二室一厅一卫", 92.0, "精装", 4100, "陈先生", "13612345671", 1005, HOUSE_RENTED},
    {2005, "沈阳", "浑南区", "幸福家园", "浑南东路56号", "5栋", 14, "1401室", "高层", "南", "二室一厅一卫", 88.0, "精装", 3950, "杨女士", "13612345672", 1006, HOUSE_VACANT},
    {2006, "沈阳", "皇姑区", "幸福里", "崇山中路30号", "2栋", 9, "902室", "中层", "南北", "二室一厅一卫", 95.0, "简装", 4050, "刘女士", "13612345673", 1007, HOUSE_VACANT},
    {2007, "沈阳", "和平区", "幸福雅苑", "太原街88号", "1栋", 11, "1103室", "中层", "南", "二室一厅一卫", 86.5, "精装", 4000, "宋先生", "13612345674", 1008, HOUSE_VACANT},
    {2008, "沈阳", "浑南区", "幸福新城", "创新路66号", "8栋", 16, "1602室", "高层", "南北", "三室一厅两卫", 102.0, "精装", 4300, "周先生", "13612345675", 1009, HOUSE_RENTED},
    {2009, "沈阳", "皇姑区", "幸福花园", "黄河南大街99号", "4栋", 7, "702室", "中层", "南", "二室一厅一卫", 90.0, "简装", 3900, "吴女士", "13612345676", 1010, HOUSE_VACANT},
    {2010, "沈阳", "浑南区", "中海国际社区", "新运河路16号", "9栋", 18, "1801室", "高层", "南北", "三室一厅两卫", 118.0, "精装", 4700, "邓先生", "13612345677", 1004, HOUSE_RENTED},
    {2011, "沈阳", "和平区", "龙湖天街公寓", "青年大街101号", "2栋", 20, "2006室", "高层", "南", "一室一卫", 58.0, "精装", 2600, "方女士", "13612345678", 1008, HOUSE_VACANT},
};

static const DemoViewingEntry kViewings[] = {
    {3001, "2026-03-20 14:30", 2001, 5001, 1002, 45, VIEWING_CONFIRMED, VIEWING_CONTRACT_NONE, "-", "已电话确认"},
    {3002, "2026-03-22 10:00", 2002, 5002, 0, 30, VIEWING_UNCONFIRMED, VIEWING_CONTRACT_NONE, "-", "-"},
    {3003, "2026-04-04 12:30", 2004, 5004, 1005, 45, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "交通方便，考虑签约", "已带看完成"},
    {3004, "2026-04-04 15:00", 2005, 5005, 1006, 60, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "房屋采光好", "已完成带看并签约"},
    {3005, "2026-04-05 12:00", 2006, 5006, 1007, 45, VIEWING_COMPLETED, VIEWING_CONTRACT_PENDING, "想再和家人商量", "已发起合同待租客确认"},
    {3006, "2026-04-05 16:30", 2007, 5007, 1008, 30, VIEWING_CONFIRMED, VIEWING_CONTRACT_NONE, "-", "已确认到场"},
    {3007, "2026-04-06 13:30", 2008, 5008, 1009, 50, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "周边配套不错", "合同已完成"},
    {3008, "2026-04-06 15:30", 2009, 5009, 1010, 40, VIEWING_COMPLETED, VIEWING_CONTRACT_PENDING, "价格略高，待确认", "合同待确认"},
    {3009, "2026-04-07 12:30", 2001, 5010, 1002, 45, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "满意，准备入住", "签约完成"},
    {3010, "2026-04-07 14:30", 2002, 5001, 1002, 35, VIEWING_COMPLETED, VIEWING_CONTRACT_NONE, "租期不合适", "租客放弃签约"},
    {3011, "2026-04-08 12:00", 2004, 5002, 1005, 30, VIEWING_CONFIRMED, VIEWING_CONTRACT_NONE, "-", "已电话确认"},
    {3012, "2026-04-08 15:00", 2002, 5003, 1002, 45, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "可以签", "合同已完成"},
    {3013, "2026-04-08 16:30", 2006, 5004, 1007, 30, VIEWING_UNCONFIRMED, VIEWING_CONTRACT_NONE, "-", "-"},
    {3014, "2026-04-05 13:30", 2007, 5007, 1008, 40, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "户型合适", "已签约完成"},
    {3015, "2026-04-07 15:30", 2010, 5002, 1004, 50, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "愿意签一年", "签约已完成"},
    {3016, "2026-04-08 12:30", 2011, 5006, 1008, 35, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "交通方便", "当天已签"},
    {3017, "2026-04-08 14:00", 2006, 5008, 1007, 45, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "满意可签", "完成带看并签约"},
    {3018, "2026-04-09 12:30", 2005, 5011, 1006, 45, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "采光不错，准备短租", "沟通顺畅，已推进签约"},
    {3019, "2026-04-10 15:00", 2007, 5011, 1008, 60, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "楼层和朝向满意", "租客满意，后续完成签约"},
    {3020, "2026-04-11 14:30", 2002, 5011, 1002, 40, VIEWING_COMPLETED, VIEWING_CONTRACT_DONE, "预算内，可长期租", "完成带看，当天签约"},
    {3021, "2026-04-12 16:00", 2009, 5011, 1010, 35, VIEWING_COMPLETED, VIEWING_CONTRACT_NONE, "备选房源，暂不签", "已带看，等待租客决定"},
    {3022, "2026-04-13 13:30", 2011, 5011, 1008, 50, VIEWING_CONFIRMED, VIEWING_CONTRACT_NONE, "-", "已确认看房安排"},
};

static const DemoRentalEntry kRentals[] = {
    {4001, 2003, 5003, 1001, 0, "2026-02-15", "2026-03-01", "2027-02-28", 12, 4300, 8600, "", "", RENTAL_ACTIVE, RENTAL_SIGN_CONFIRMED},
    {4002, 2004, 5004, 1005, 3003, "2026-04-04", "2026-04-10", "2027-04-09", 12, 4100, 8200, "首年不涨租", "", RENTAL_ACTIVE, RENTAL_SIGN_CONFIRMED},
    {4003, 2005, 5005, 1006, 3004, "2026-04-04", "2026-04-12", "2027-04-11", 12, 3950, 7900, "含物业费", "租客提前退租，房源重新上架", RENTAL_EARLY, RENTAL_SIGN_CONFIRMED},
    {4004, 2006, 5006, 1007, 3005, "2026-04-05", "2026-04-15", "2027-04-14", 12, 4050, 8100, "可月付", "租客暂缓签约", RENTAL_EARLY, RENTAL_SIGN_REJECTED},
    {4005, 2008, 5008, 1009, 3007, "2026-04-06", "2026-04-20", "2027-04-19", 12, 4300, 8600, "车位优先", "", RENTAL_ACTIVE, RENTAL_SIGN_CONFIRMED},
    {4006, 2009, 5009, 1010, 3008, "2026-04-06", "2026-04-18", "2027-04-17", 12, 3900, 7800, "可养宠物", "", RENTAL_ACTIVE, RENTAL_SIGN_PENDING},
    {4007, 2001, 5010, 1002, 3009, "2026-04-07", "2026-04-16", "2027-04-15", 12, 3200, 6400, "可季度付", "", RENTAL_ACTIVE, RENTAL_SIGN_CONFIRMED},
    {4008, 2002, 5001, 1002, 3010, "2026-04-07", "2026-04-17", "2027-04-16", 12, 2100, 4200, "", "租客预算不足，拒绝签约", RENTAL_EARLY, RENTAL_SIGN_REJECTED},
    {4009, 2002, 5003, 1002, 3012, "2026-04-08", "2026-04-22", "2027-04-21", 12, 2200, 4400, "首月减免200元", "租客工作调动提前解约", RENTAL_EARLY, RENTAL_SIGN_CONFIRMED},
    {4010, 2007, 5007, 1008, 3014, "2026-04-05", "2026-04-12", "2027-04-11", 12, 4000, 8000, "可半年付", "租期变更后提前结束", RENTAL_EARLY, RENTAL_SIGN_CONFIRMED},
    {4011, 2010, 5002, 1004, 3015, "2026-04-07", "2026-04-20", "2027-04-19", 12, 4700, 9400, "含车位管理费", "", RENTAL_ACTIVE, RENTAL_SIGN_CONFIRMED},
    {4012, 2011, 5006, 1008, 3016, "2026-04-08", "2026-04-18", "2027-04-17", 12, 2600, 5200, "可两年续租优先", "租客转租，合同终止", RENTAL_EARLY, RENTAL_SIGN_CONFIRMED},
    {4013, 2006, 5008, 1007, 3017, "2026-04-08", "2026-04-25", "2027-04-24", 12, 4050, 8100, "首年物业减半", "双方协商提前结束", RENTAL_EARLY, RENTAL_SIGN_CONFIRMED},
    {4014, 2005, 5011, 1006, 3018, "2026-04-09", "2026-04-15", "2026-10-14", 6, 3980, 7960, "短租6个月", "工作地点变动提前结束", RENTAL_EARLY, RENTAL_SIGN_CONFIRMED},
    {4015, 2007, 5011, 1008, 3019, "2026-04-10", "2026-04-20", "2026-12-19", 8, 4050, 8100, "可按季支付", "升级换租提前退租", RENTAL_EARLY, RENTAL_SIGN_CONFIRMED},
    {4016, 2002, 5011, 1002, 3020, "2026-04-11", "2026-04-25", "2027-04-24", 12, 2180, 4360, "首月减免100", "", RENTAL_ACTIVE, RENTAL_SIGN_CONFIRMED},
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
