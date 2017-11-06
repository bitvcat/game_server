#include <stdio.h>
#include <unistd.h>

//lua
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "luuid.h"
#include "lsnowflake.h"

#include "ltime_wheel.h"

/*
uuid
snowflake
aoi
a星寻路
双数组字典树屏蔽字
postgre sql 驱动
地图
定时器（时间轮）
*/

int main(int argc, char** argv)
{
	printf("hello world!\n");

	lua_State *L = luaL_newstate();
	luaL_openlibs(L);  // 加载Lua通用扩展库

	luaL_requiref(L,"luuid",luaopen_uuid,1);
	luaL_requiref(L,"lsnowflake",luaopen_snowflake,1);

	int ret = luaL_dofile(L,"../test.lua");
	printf("ret = %d\n", ret);

	lua_close(L);

	printf("\n\n");
	create_timer();
	add_timer(100);
	for (;;) {
		int32_t sleepMs = timer_updatetime();
		//printf("loop ……, sleepMs=%d\n",sleepMs);
		if(sleepMs<0){
			break;
		}else{
			usleep(sleepMs*1000);
		}
	}

	destroy_timer();
	return 0;
}
