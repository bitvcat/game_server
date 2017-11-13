#include <stdio.h>
#include <errno.h>

// uuid
#include "uuid.h"
#include "lbase64.h"
#include "lsnowflake.h"

#include "lenv.h"
#include "app.h"

static int ltimeCallback(uint32_t id, void *clientData){
	printf("ltimeCallback : timer id = %d\n", id);
	appServer *app = (appServer *)clientData;
	lua_State *L = app->L;

	int top = lua_gettop(L);
	int rType = lua_getglobal(L,"onTimer");
	int status = lua_pcall(L,0,1,0);
	if (status != LUA_OK) {
		printf("timer cb err = %s\n", lua_tostring(L,-1));
		return;
	}else{
		if (lua_isinteger(L,-1)){
			//printf("onTimer result = %d\n",lua_tointeger(L,-1));
		}
	}
	lua_settop(L,top);
	return 0;
}

/* =========================== lua api =========================== */
static int lc_uuid(lua_State *L){
	// uuid
	uuid_t uu;
	uuid_generate(uu);

	size_t outSize;
	unsigned char *uuidstr = base64_encode(uu, sizeof(uu), &outSize);
	uuidstr[22] = '\0';
	for (int i = 0; i < 22; ++i){
		if (*(uuidstr+i)=='/'){
			*(uuidstr+i) = '-'; // 替换"/"
		}
	}
	lua_pushlstring(L,uuidstr,22);
	return 1;
}

// snowflake
static int lc_snowflake_init(lua_State* l) {
    int16_t work_id = 0;
    if (lua_gettop(l) > 0) {
        lua_Integer id = luaL_checkinteger(l, 1);
        if (id < 0 || id > MAX_WORKID_VAL) {
            return luaL_error(l, "Work id is in range of 0 - 1023.");
        }
        work_id = (int16_t)id;
    }
    if (snowflakeInit(work_id)) {
        return luaL_error(l, "Snowflake has been initialized.");
    }
    lua_pushboolean(l, 1);
    return 1;
}

static int lc_snowflake_nextid(lua_State* l) {
    int64_t id = snowflakeNextId();
    lua_pushinteger(l, (lua_Integer)id);
    return 1;
}

// net
static int lc_net_connect(lua_State *L){
	// 如果是同一个物理机，可以使用unix域套接字
	// 否则，使用tcp socket 连接
	appServer *app = lua_touserdata(L, lua_upvalueindex(1));
	if (app==NULL){
		return luaL_error(L, "lc_net_connect error");
	}

	char *addr = luaL_checkstring(L, -1);
	int port = (int)luaL_checkinteger(L, -2);
	int fd = netConnect(addr, port);
	lua_pushinteger(L, fd);
	return 1;
}

static int lc_net_listen(lua_State *L){
	appServer *app = lua_touserdata(L, lua_upvalueindex(1));
	if (app==NULL){
		return luaL_error(L, "lc_net_connect error");
	}

	char *addr = luaL_checkstring(L, 1);
	int port = (int)luaL_checkinteger(L, 2);
	int fd = netListen(port, addr);
	if (fd<0) {
		return luaL_error(L, "Unrecoverable error creating server.ipfd file event.");
	}
	lua_pushinteger(L, fd);
	return 1;
}

static int lc_net_sendmsg(lua_State *L){

	// lua_sendMsg(fd,toTy,toId,cmd,pkt,pid);
	return 0;
}

// timer
static int lc_timer_add(lua_State *L){
	appServer *app = lua_touserdata(L, lua_upvalueindex(1));
	if (app==NULL){
		return luaL_error(L, "lc_add_timer error");
	}

	lua_Integer tickNum = lua_tointeger(L, 1);
	uint32_t timerId = aeAddTimer(app->pEl, tickNum, ltimeCallback, app);
	lua_pushinteger(L, timerId);
	return 1;
}

static int lc_timer_del(lua_State *L){
	appServer *app = lua_touserdata(L, lua_upvalueindex(1));
	if (app==NULL){
		return luaL_error(L, "lc_del_timer error");
	}

	lua_Integer timerId = lua_tointeger(L, 1);
	aeDelTimer(app->pEl, (uint32_t)timerId);
	return 0;
}


// c lua lib
int luaopen_env(struct lua_State* L){
	luaL_checkversion(L);

	luaL_Reg l[] = {
		{ "uuid", lc_uuid },
		{ "sf_init", lc_snowflake_init },
        { "sf_nextid", lc_snowflake_nextid },
		{ "net_connect", lc_net_connect },
		{ "net_listen", lc_net_listen },
		{ "addTimer", lc_timer_add },
		{ "delTimer", lc_timer_del },
		{ NULL,  NULL }
	};

	luaL_newlibtable(L, l);
	lua_getfield(L, LUA_REGISTRYINDEX, "app_context");
	appServer *app = lua_touserdata(L,-1);
	if (app == NULL) {
		return luaL_error(L, "Init app context first ");
	}
	luaL_setfuncs(L,l,1);//注册，并共享一个upvalue
	return 1;
}