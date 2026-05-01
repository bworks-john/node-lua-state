#include <napi.h>
#include <variant>

#include "lua-state.h"

/**
 * Napiapi Initializer
 */
void LuaState::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function lua_state_class = DefineClass(
    env,
    "LuaState",
    {
      InstanceMethod("evalFile", &LuaState::EvalLuaFile),
      InstanceMethod("eval", &LuaState::EvalLuaString),
      InstanceMethod("getGlobal", &LuaState::GetLuaGlobalValue),
      InstanceMethod("getLength", &LuaState::GetLuaValueLength),
      InstanceMethod("getVersion", &LuaState::GetLuaVersion),
      InstanceMethod("setGlobal", &LuaState::SetLuaGlobalValue),
      InstanceMethod("setMemoryCallback", &LuaState::SetMemoryManagedContextAllocatorCallback),
      InstanceMethod("setMaxPermittedMemory", &LuaState::SetMaximumExecutionMemory),
      InstanceMethod("setMaxPermittedTime", &LuaState::SetMaximumExecutionTime),
    }
  );

  Napi::FunctionReference* constructor = new Napi::FunctionReference(Napi::Persistent(lua_state_class));
  env.SetInstanceData(constructor);

  exports.Set("LuaState", lua_state_class);
}

/**
 * Constructor
 */
LuaState::LuaState(const Napi::CallbackInfo& info) : Napi::ObjectWrap<LuaState>(info) {
  Napi::Env env = info.Env();
  ctx_ = new LuaStateContext(&env);

  auto open_all_libs = true;
  std::vector<std::string> libs_for_open;

  if (info.Length() == 1 && info[0].IsObject()) {
    auto options = info[0].As<Napi::Object>();

    if (options.Has("libs")) {
      auto libs_option = options.Get("libs");

      if (libs_option.IsNull()) {
        open_all_libs = false;
      } else if (libs_option.IsArray()) {
        open_all_libs = false;
        auto lib_names = libs_option.As<Napi::Array>();

        for (size_t i = 0; i < lib_names.Length(); i++) {
          std::string lib_name = lib_names.Get(i).ToString().Utf8Value();
          libs_for_open.push_back(lib_name);
        }
      }
    }
  }

  if (open_all_libs) {
    ctx_->OpenLibs(std::nullopt);
  } else {
    ctx_->OpenLibs(libs_for_open);
  }
}

LuaState::~LuaState() { delete ctx_; }

/**
 * EvalLuaFile
 */
Napi::Value LuaState::EvalLuaFile(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String argument expected").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto file_path = info[0].ToString().Utf8Value();
  auto eval_result = ctx_->EvalFile(env, file_path);

  if (std::holds_alternative<Napi::Error>(eval_result)) {
    std::get<Napi::Error>(eval_result).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  return std::get<Napi::Value>(eval_result);
}

/**
 * EvalLuaString
 */
Napi::Value LuaState::EvalLuaString(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String argument expected").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto lua_code = info[0].As<Napi::String>().Utf8Value();
  auto eval_result = ctx_->EvalString(env, lua_code);

  if (std::holds_alternative<Napi::Error>(eval_result)) {
    std::get<Napi::Error>(eval_result).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  return std::get<Napi::Value>(eval_result);
}

/**
 * GetLuaGlobalValue
 */
Napi::Value LuaState::GetLuaGlobalValue(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String argument expected").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::string lua_value_path = info[0].As<Napi::String>();

  return ctx_->GetLuaValueByPath(env, lua_value_path);
}

/**
 * GetLuaValueLength
 */
Napi::Value LuaState::GetLuaValueLength(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String argument expected").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::string lua_value_path = info[0].As<Napi::String>();

  return ctx_->GetLuaValueLengthByPath(env, lua_value_path);
}

/**
 * GetLuaVersion
 */
Napi::Value LuaState::GetLuaVersion(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  auto version = ctx_->GetLuaVersion();
  return Napi::String::New(env, version);
}

/**
 * SetLuaGlobalValue
 */
Napi::Value LuaState::SetLuaGlobalValue(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  if (info.Length() < 2 || !info[0].IsString()) {
    Napi::TypeError::New(env, "First argument expected string").ThrowAsJavaScriptException();
    return info.This();
  }

  std::string name = info[0].As<Napi::String>();
  auto value = info[1].As<Napi::Value>();

  ctx_->SetLuaValue(name, value);

  return info.This();
}

Napi::Value LuaState::SetMemoryManagedContextAllocatorCallback(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(env, "parameter 1 to setMemoryCallback must be function").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Function funcdecl = info[0].As<Napi::Function>();
  return ctx_->SetMemoryManagedContextAllocatorCallback(env, funcdecl);
}

Napi::Value LuaState::SetMaximumExecutionMemory(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "parameter 1 to setMaximumExecutionMemory must be numeric").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Number number = info[0].As<Napi::Number>();
  uint64_t result = ctx_->SetMaxPermittedBytes(number.Uint32Value());

  return Napi::Number::New(env, result);
}

Napi::Value LuaState::SetMaximumExecutionTime(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "parameter 1 to setMaximumExecutionTime must be numeric").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Number number = info[0].As<Napi::Number>();
  long result = ctx_->SetMaxPermittedTime(number.FloatValue());

  return Napi::Number::New(env, result);
}

Napi::Value LuaState::ResetExecutionClock(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  bool result;

  try {
    result = ctx_->ResetElapsedClock(true);
  } catch (const IllegalOperationException& ioe) {
    Napi::Error::New(env, std::string(ioe.what())).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  return Napi::Boolean::New(env, result);
}
