#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

/* enum of VM types: either NAPI or LVM */
typedef enum VMType { NAPI, LVM } VMType;

/* container for lua_State and Napi::Env smuggling */
typedef struct CrossVMCallBoundaryInfo {
  const lua_State* L;
  const Napi::Env* env;
} CrossVMCallBoundaryInfo;

/*
 * maximum number of VM instances; this gets used as part of pack/unpack and
 * template expansions later -- be careful with this number
 */
constexpr size_t MAX_MMC_SLOTS = 128;

/* exception thrown from any LVM context when time has expired */
class OutOfTimeException : std::exception {
public:
  const char* what() const noexcept { return "maximum execution time exceeded"; }
};

/* exception thrown from any LVM context when memory is exhausted */
class OutOfMemoryException : std::exception {
public:
  const char* what() const noexcept { return "maximum memory budget exceeded"; }
};

/* exception thrown from any LVM context when lua_panic fires */
class PanicFromLuaException : std::exception {
public:
  const char* what() const noexcept { return "panic request from Lua VM"; }
};

/* any invalid or non-permitted operation exception */
class IllegalOperationException : std::exception {
public:
  const char* what() const noexcept { return "illegal operation"; }
};

class LuaStateContext {
public:
  explicit LuaStateContext(Napi::Env* env);
  ~LuaStateContext();

  static void Init(Napi::Env, Napi::Object);
  static LuaStateContext* From(lua_State*);

  void OpenLibs(const std::optional<std::vector<std::string>>&);

  std::variant<Napi::Value, Napi::Error> EvalFile(const Napi::Env&, const std::string&);
  std::variant<Napi::Value, Napi::Error> EvalString(const Napi::Env&, const std::string&);

  void SetLuaValue(const std::string&, const Napi::Value&);

  Napi::Value GetLuaValueByPath(const Napi::Env&, const std::string&);
  Napi::Value GetLuaValueLengthByPath(const Napi::Env&, const std::string&);
  std::string GetLuaVersion();

  Napi::Function FindOrCreateJsFunction(const Napi::Env&, int);

  /**************************** MM FUNCTIONS ****************************/

  /*
   * set the NAPI ufunc which will control memory allocation requests; the assigned function
   * must return true if the memory allocation is allowed. returning any other value results
   * in the allocation failing.
   *
   * when the allocation fails, the memory which was being resized is fed to free() and NULL
   * returned to LVM; it is then LVM's decision as to how to proceed, but, usually, the
   * entire VM should be torn down.
   *
   * the allocator is bypassed if the size of the request is zero (ie, LVM is freeing the
   * memory).
   *
   * any other behaviour outside of this is considered unspecified and should result in LVM
   * shutting down.
   */
  Napi::Value SetMemoryManagedContextAllocatorCallback(const Napi::Env& env, const Napi::Value& value);

  /* get allocated bytes total (current live page size) */
  uint64_t GetAllocatedBytes();
  /* get peak allocated bytes (maximum live page size) */
  uint64_t GetPeakAllocatedBytes();
  /* get hard limit of allocated bytes assigned, or -1 if no limit */
  uint64_t GetMaxPermittedBytes();

  /*
   * set the absolute maximum number of bytes this container may consume; this check
   * occurs before calling any NAPI ufunc to confirm that NAPI user-code allows the
   * allocation to occur.
   *
   * if the memory limit has been hit, or would be hit by the allocation, either in
   * part or in full, then the request is refused, the block referenced fed to free();
   * it is then LVM's decision as to how to proceed, but, usually, the entire VM
   * should be torn down.
   *
   * the allocator is bypassed if the size of the request is zero (ie, LVM is freeing
   * the memory).
   */
  uint64_t SetMaxPermittedBytes(uint64_t size);

  /* get currently elapsed time, imprecise */
  long GetCurrentElapsedTime();

  /*
   * set the CPU time budget for this instance; if set, when any single 'entrant
   * call' to LVM does not complete within the budgeted time, the LVM instance is
   * interrupted by an exception.
   */
  long SetMaxPermittedTime(long max_seconds);

  /*
   * reset the CPU time budget for this instance; if throw_if_locked is set and
   * the call to this method is performed while LVM is active, then an exception
   * will be thrown up the stack (to prevent LVM instances from self-resetting
   * their own timer, either accidentally or deliberately). if throw_if_locked
   * is not set, then the timer is reset immediately. if successful, returns true.
   */
  bool ResetElapsedClock(bool throw_if_locked);

  /*
   * called when a context-switch occurs between LVM and NAPI; the origin
   * is the current context and the target is the destination of the cross. the
   * cinfo container contains pointer(s) to the origin and target if they
   * are available, as well as any other additional information which might
   * be useful to describe the context-switch.
   */
  void OnContextSwitch(VMType origin, VMType target, CrossVMCallBoundaryInfo* cinfo);

  /*
   * called internally to update memory accounting
   */
  int UpdateAccounting(size_t osize, size_t nsize);

  /*
   * part of the IJT hackery for passing C++ pointer of instance this to C
   */
  void* AllocateMemoryForLua(void* ud, void* ptr, size_t osize, size_t nsize);

  /**
   * part of the IJT hackery for passing C++ pointer of instance this to C
   */
  int PanicFromLua(lua_State* L);

  /**
   * part of the IJT hackery for passing C++ pointer of instance this to C
   */
  void HookFromLua(lua_State* L, lua_Debug* ar);

  static volatile int nextId_;

protected:
  /* our instance id, also slot in IJT */
  int instanceId;
  /* our lua_State LVM inst */
  lua_State* L_;

  static inline std::unordered_map<lua_State*, LuaStateContext*> contexts_;

private:
  std::unordered_map<const void*, Napi::Function> js_functions_cache_;
  std::mutex js_functions_cache_mtx_;

  /* our allocator function, or nullptr */
  Napi::FunctionReference* jsAllocatorFunc_;

  /* our allocated bytes */
  uint64_t allocated_bytes;
  /* our peak allocated bytes */
  uint64_t peak_allocated_bytes;
  /* our max allowed bytes, or -1 */
  uint64_t max_permitted_bytes;

  /* our timer status */
  bool timer_running;
  /* our depth level, 0 = not in reentrant section, >0 = in LVM or reentrant to NAPI */
  int timer_reentrant_depth;
  /* our elapsed time thus far */
  timespec elapsed_seconds;
  /* the last time we checked the time, because we transitioned into LVM */
  timespec timer_last_start;
  /* the last time we stopped checking time, either because we transitioned out of LVM or into NAPI again */
  timespec timer_last_stop;

  /* the max amount of CPU time one reentrant section can consume, or -1 if unrestricted */
  time_t max_execution_seconds;
};

/* type used for our boundary callback handler function */
typedef void (*LuaContext_BoundaryCallback)(VMType origin, VMType target, CrossVMCallBoundaryInfo* cinfo);

/* our context hook scope:  (FIXME: demangle name) */
template <size_t id> struct LuaStateMemoryManagedContextHooks {
  static inline LuaStateContext* context = nullptr;

  static void DebugHook(lua_State* L, lua_Debug* df) {
    if (context != nullptr) {
      context->HookFromLua(L, df);
    }
  }

  static int PanicHook(lua_State* L) {
    if (context != nullptr) {
      context->PanicFromLua(L);
    }
    return 0;
  }

  static void SwitchVMHook(VMType origin, VMType target, CrossVMCallBoundaryInfo* cinfo) {
    if (context != nullptr) {
      context->OnContextSwitch(origin, target, cinfo);
    }
  }

  static void* AllocHook(void* ud, void* ptr, size_t osize, size_t nsize) {
    if (context != nullptr) {
      return context->AllocateMemoryForLua(ud, ptr, osize, nsize);
    } else {
      /*
       * if we don't have a context assigned, then something has probably gone very wrong;
       * we should probably abort, but for testing we'll behave like l_alloc (lauxlib.h),
       * as if we were never hooking the VM at all:
       */
      if (nsize == 0) {
        free(ptr);
        return NULL;
      } else
        return realloc(ptr, nsize);
    }
  }
};

/* our IJT layout:  (FIXME: demangle name) */
struct LuaStateMemoryManagedContextHookFuncs {
  lua_Alloc alloc;
  lua_Hook debug;
  lua_CFunction panic;
  LuaContext_BoundaryCallback switchscope;
};

/* our IJT generator: (FIXME: demangle name) */
template <size_t... jt_size> auto GenerateLuaStateMemoryManagedContextHookFuncsTable(std::index_sequence<jt_size...>) {
  return std::array<LuaStateMemoryManagedContextHookFuncs, sizeof...(jt_size)>{
    {{(lua_Alloc)&LuaStateMemoryManagedContextHooks<jt_size>::AllocHook,
      (lua_Hook)&LuaStateMemoryManagedContextHooks<jt_size>::DebugHook,
      (lua_CFunction)&LuaStateMemoryManagedContextHooks<jt_size>::PanicHook,
      (LuaContext_BoundaryCallback)&LuaStateMemoryManagedContextHooks<jt_size>::SwitchVMHook}...}
  };
};

/* our IJT itself: (FIXME: demangle name) */
static auto LuaStateMemoryManagedContextHookFuncsTable = GenerateLuaStateMemoryManagedContextHookFuncsTable(std::make_index_sequence<MAX_MMC_SLOTS>{});

/*
 * try assign a slot on the IJT to some contextptr; this isn't exactly great
 * since it produces some awful-looking bytecode, but, since we only load the
 * module ELF once, it's not *that* bad.... kinda
 */
template <size_t... jt_size>
static bool LuaStateMemoryManagedContextHooks__TryAssignInstance(size_t slot, LuaStateContext* context, std::index_sequence<jt_size...> seq) {
  return ((slot == jt_size ? (LuaStateMemoryManagedContextHooks<jt_size>::context = context, true) : false) || ...);
};
