#include <node.h>
#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include <iostream>
#include "module.h"
#include "process.h"
#include "memoryjs.h"
#include "memory.h"
#include "pattern.h"

using v8::Exception;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Number;
using v8::Value;
using v8::Handle;
using v8::Array;
using v8::Boolean;

process Process;
module Module;
memory Memory;
pattern Pattern;

struct Vector3 {
  float x, y, z;
};

struct Vector4 {
  float w, x, y, z;
};

void memoryjs::throwError(char* error, Isolate* isolate) {
  isolate->ThrowException(
    Exception::TypeError(String::NewFromUtf8(isolate, error))
  );
  return;
}

void openProcess(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  
  if (args.Length() != 1 && args.Length() != 2) {
    memoryjs::throwError("requires 1 argument, or 2 arguments if a callback is being used", isolate);
    return;
  }

  if (!args[0]->IsString() && !args[0]->IsNumber()) {
    memoryjs::throwError("first argument must be a string or a number", isolate);
    return;
  }

  if (args.Length() == 2 && !args[1]->IsFunction()) {
    memoryjs::throwError("second argument must be a function", isolate);
    return;
  }

  // Define error message that may be set by the function that opens the process
  char* errorMessage = "";

  process::Pair pair;

  if (args[0]->IsString()) {
    v8::String::Utf8Value processName(args[0]);  
    pair = Process.openProcess((char*) *(processName), &errorMessage);

    // In case it failed to open, let's keep retrying
    // while(!strcmp(process.szExeFile, "")) {
    //   process = Process.openProcess((char*) *(processName), &errorMessage);
    // };
  }

  if (args[0]->IsNumber()) {
    pair = Process.openProcess(args[0]->Uint32Value(), &errorMessage);

    // In case it failed to open, let's keep retrying
    // while(!strcmp(process.szExeFile, "")) {
    //   process = Process.openProcess(args[0]->Uint32Value(), &errorMessage);
    // };
  }

  // If an error message was returned from the function that opens the process, throw the error.
  // Only throw an error if there is no callback (if there's a callback, the error is passed there).
  if (strcmp(errorMessage, "") && args.Length() != 2) {
    memoryjs::throwError(errorMessage, isolate);
    return;
  }

  // Create a v8 Object (JSON) to store the process information
  Local<Object> processInfo = Object::New(isolate);

  processInfo->Set(String::NewFromUtf8(isolate, "dwSize"), Number::New(isolate, (int)pair.process.dwSize));
  processInfo->Set(String::NewFromUtf8(isolate, "th32ProcessID"), Number::New(isolate, (int)pair.process.th32ProcessID));
  processInfo->Set(String::NewFromUtf8(isolate, "cntThreads"), Number::New(isolate, (int)pair.process.cntThreads));
  processInfo->Set(String::NewFromUtf8(isolate, "th32ParentProcessID"), Number::New(isolate, (int)pair.process.th32ParentProcessID));
  processInfo->Set(String::NewFromUtf8(isolate, "pcPriClassBase"), Number::New(isolate, (int)pair.process.pcPriClassBase));
  processInfo->Set(String::NewFromUtf8(isolate, "szExeFile"), String::NewFromUtf8(isolate, pair.process.szExeFile));
  processInfo->Set(String::NewFromUtf8(isolate, "handle"), Number::New(isolate, (int)pair.handle));

  DWORD base = Module.getBaseAddress(pair.process.szExeFile, pair.process.th32ProcessID);
  processInfo->Set(String::NewFromUtf8(isolate, "modBaseAddr"), Number::New(isolate, (uintptr_t)base));

  // openProcess can either take one argument or can take
  // two arguments for asychronous use (second argument is the callback)
  if (args.Length() == 2) {
    // Callback to let the user handle with the information
    Local<Function> callback = Local<Function>::Cast(args[1]);
    const unsigned argc = 2;
    Local<Value> argv[argc] = { String::NewFromUtf8(isolate, errorMessage), processInfo };
    callback->Call(Null(isolate), argc, argv);
  } else {
    // return JSON
    args.GetReturnValue().Set(processInfo);
  }
}

void closeProcess(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();

  if (args.Length() != 1) {
    memoryjs::throwError("requires 1 argument", isolate);
    return;
  }

  if (!args[0]->IsNumber()) {
    memoryjs::throwError("first argument must be a number", isolate);
    return;
  }

  Process.closeProcess((HANDLE)args[0]->Int32Value());
}

void getProcesses(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();

  if (args.Length() > 1) {
    memoryjs::throwError("requires either 0 arguments or 1 argument if a callback is being used", isolate);
    return;
  }

  if (args.Length() == 1 && !args[0]->IsFunction()) {
    memoryjs::throwError("first argument must be a function", isolate);
    return;
  }

  // Define error message that may be set by the function that gets the processes
  char* errorMessage = "";

  std::vector<PROCESSENTRY32> processEntries = Process.getProcesses(&errorMessage);

  // If an error message was returned from the function that gets the processes, throw the error.
  // Only throw an error if there is no callback (if there's a callback, the error is passed there).
  if (strcmp(errorMessage, "") && args.Length() != 1) {
    memoryjs::throwError(errorMessage, isolate);
    return;
  }

  // Creates v8 array with the size being that of the processEntries vector processes is an array of JavaScript objects
  Handle<Array> processes = Array::New(isolate, processEntries.size());

  // Loop over all processes found
  for (std::vector<PROCESSENTRY32>::size_type i = 0; i != processEntries.size(); i++) {
    // Create a v8 object to store the current process' information
    Local<Object> process = Object::New(isolate);

    process->Set(String::NewFromUtf8(isolate, "cntThreads"), Number::New(isolate, (int)processEntries[i].cntThreads));
    process->Set(String::NewFromUtf8(isolate, "szExeFile"), String::NewFromUtf8(isolate, processEntries[i].szExeFile));
    process->Set(String::NewFromUtf8(isolate, "th32ProcessID"), Number::New(isolate, (int)processEntries[i].th32ProcessID));
    process->Set(String::NewFromUtf8(isolate, "th32ParentProcessID"), Number::New(isolate, (int)processEntries[i].th32ParentProcessID));
    process->Set(String::NewFromUtf8(isolate, "pcPriClassBase"), Number::New(isolate, (int)processEntries[i].pcPriClassBase));

    // Push the object to the array
    processes->Set(i, process);
  }

  /* getProcesses can either take no arguments or one argument
     one argument is for asychronous use (the callback) */
  if (args.Length() == 1) {
    // Callback to let the user handle with the information
    Local<Function> callback = Local<Function>::Cast(args[0]);
    const unsigned argc = 2;
    Local<Value> argv[argc] = { String::NewFromUtf8(isolate, errorMessage), processes };
    callback->Call(Null(isolate), argc, argv);
  } else {
    // return JSON
    args.GetReturnValue().Set(processes);
  }
}

void getModules(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();

  if (args.Length() != 1 && args.Length() != 2) {
    memoryjs::throwError("requires 1 argument, or 2 arguments if a callback is being used", isolate);
    return;
  }

  if (!args[0]->IsNumber()) {
    memoryjs::throwError("first argument must be a number", isolate);
    return;
  }

  if (args.Length() == 2 && !args[1]->IsFunction()) {
    memoryjs::throwError("first argument must be a number, second argument must be a function", isolate);
    return;
  }

  // Define error message that may be set by the function that gets the modules
  char* errorMessage = "";

  std::vector<MODULEENTRY32> moduleEntries = Module.getModules(args[0]->Int32Value(), &errorMessage);

  // If an error message was returned from the function getting the modules, throw the error.
  // Only throw an error if there is no callback (if there's a callback, the error is passed there).
  if (strcmp(errorMessage, "") && args.Length() != 2) {
    memoryjs::throwError(errorMessage, isolate);
    return;
  }

  // Creates v8 array with the size being that of the moduleEntries vector
  // modules is an array of JavaScript objects
  Handle<Array> modules = Array::New(isolate, moduleEntries.size());

  // Loop over all modules found
  for (std::vector<MODULEENTRY32>::size_type i = 0; i != moduleEntries.size(); i++) {
    //  Create a v8 object to store the current module's information
    Local<Object> module = Object::New(isolate);

    module->Set(String::NewFromUtf8(isolate, "modBaseAddr"), Number::New(isolate, (uintptr_t)moduleEntries[i].modBaseAddr));
    module->Set(String::NewFromUtf8(isolate, "modBaseSize"), Number::New(isolate, (int)moduleEntries[i].modBaseSize));
    module->Set(String::NewFromUtf8(isolate, "szExePath"), String::NewFromUtf8(isolate, moduleEntries[i].szExePath));
    module->Set(String::NewFromUtf8(isolate, "szModule"), String::NewFromUtf8(isolate, moduleEntries[i].szModule));
    module->Set(String::NewFromUtf8(isolate, "th32ModuleID"), Number::New(isolate, (int)moduleEntries[i].th32ProcessID));

    // Push the object to the array
    modules->Set(i, module);
  }

  // getModules can either take one argument or two arguments
  // one/two arguments is for asychronous use (the callback)
  if (args.Length() == 2) {
    // Callback to let the user handle with the information
    Local<Function> callback = Local<Function>::Cast(args[1]);
    const unsigned argc = 2;
    Local<Value> argv[argc] = { String::NewFromUtf8(isolate, errorMessage), modules };
    callback->Call(Null(isolate), argc, argv);
  } else {
    // return JSON
    args.GetReturnValue().Set(modules);
  }
}

void findModule(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();

  if (args.Length() != 1 && args.Length() != 2 && args.Length() != 3) {
    memoryjs::throwError("requires 1 argument, 2 arguments, or 3 arguments if a callback is being used", isolate);
    return;
  }

  if (!args[0]->IsString() && !args[1]->IsNumber()) {
    memoryjs::throwError("first argument must be a string, second argument must be a number", isolate);
    return;
  }

  if (args.Length() == 3 && !args[2]->IsFunction()) {
    memoryjs::throwError("third argument must be a function", isolate);
    return;
  }
	
  v8::String::Utf8Value moduleName(args[0]);
	
  // Define error message that may be set by the function that gets the modules
  char* errorMessage = "";

  MODULEENTRY32 module = Module.findModule((char*) *(moduleName), args[1]->Int32Value(), &errorMessage);

  // If an error message was returned from the function getting the module, throw the error.
  // Only throw an error if there is no callback (if there's a callback, the error is passed there).
  if (strcmp(errorMessage, "") && args.Length() != 3) {
    memoryjs::throwError(errorMessage, isolate);
    return;
  }

  // In case it failed to open, let's keep retrying
  while (!strcmp(module.szExePath, "")) {
    module = Module.findModule((char*) *(moduleName), args[1]->Int32Value(), &errorMessage);
  };

  // Create a v8 Object (JSON) to store the process information
  Local<Object> moduleInfo = Object::New(isolate);

  moduleInfo->Set(String::NewFromUtf8(isolate, "modBaseAddr"), Number::New(isolate, (uintptr_t)module.modBaseAddr));
  moduleInfo->Set(String::NewFromUtf8(isolate, "modBaseSize"), Number::New(isolate, (int)module.modBaseSize));
  moduleInfo->Set(String::NewFromUtf8(isolate, "szExePath"), String::NewFromUtf8(isolate, module.szExePath));
  moduleInfo->Set(String::NewFromUtf8(isolate, "szModule"), String::NewFromUtf8(isolate, module.szModule));
  moduleInfo->Set(String::NewFromUtf8(isolate, "th32ProcessID"), Number::New(isolate, (int)module.th32ProcessID));
  moduleInfo->Set(String::NewFromUtf8(isolate, "hModule"), Number::New(isolate, (uintptr_t)module.hModule));

  // findModule can either take one or two arguments,
  // three arguments for asychronous use (third argument is the callback)
  if (args.Length() == 3) {
    // Callback to let the user handle with the information
    Local<Function> callback = Local<Function>::Cast(args[2]);
    const unsigned argc = 2;
    Local<Value> argv[argc] = { String::NewFromUtf8(isolate, errorMessage), moduleInfo };
    callback->Call(Null(isolate), argc, argv);
  } else {
    // return JSON
    args.GetReturnValue().Set(moduleInfo);
  }
}

void readMemory(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();

  std::cout<< args.Length() << std::endl;

  if (args.Length() != 3 && args.Length() != 4) {
    memoryjs::throwError("requires 3 arguments, or 4 arguments if a callback is being used", isolate);
    return;
  }

  if (!args[0]->IsNumber() && !args[1]->IsNumber() && !args[2]->IsString()) {
    memoryjs::throwError("first and second argument must be a number, third argument must be a string", isolate);
    return;
  }

  if (args.Length() == 4 && !args[3]->IsFunction()) {
    memoryjs::throwError("fourth argument must be a function", isolate);
    return;
  }

  v8::String::Utf8Value dataTypeArg(args[2]);
  char* dataType = (char*) *(dataTypeArg);

  // Set callback variables in the case the a callback parameter has been passed
  Local<Function> callback = Local<Function>::Cast(args[3]);
  const unsigned argc = 2;
  Local<Value> argv[argc];

  // Define the error message that will be set if no data type is recognised
  argv[0] = String::NewFromUtf8(isolate, "");

  // following if statements find the data type to read and then return the correct data type
  // args[0] -> Uint32Value() is the handle of the process
  // args[1] -> Uint32Value() is the address to read
  if (!strcmp(dataType, "int")) {

    int result = Memory.readMemory<int>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value());
    if (args.Length() == 4) argv[1] = Number::New(isolate, result);
    else args.GetReturnValue().Set(Number::New(isolate, result));

  } else if (!strcmp(dataType, "dword")) {

    DWORD result = Memory.readMemory<DWORD>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value());
    if (args.Length() == 4) argv[1] = Number::New(isolate, result);
    else args.GetReturnValue().Set(Number::New(isolate, result));

  } else if (!strcmp(dataType, "long")) {

    long result = Memory.readMemory<long>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value());
    if (args.Length() == 4) argv[1] = Number::New(isolate, result);
    else args.GetReturnValue().Set(Number::New(isolate, result));

  } else if (!strcmp(dataType, "float")) {

    float result = Memory.readMemory<float>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value());
    if (args.Length() == 4) argv[1] = Number::New(isolate, result);
    else args.GetReturnValue().Set(Number::New(isolate, result));

  } else if (!strcmp(dataType, "double")) {
		
    double result = Memory.readMemory<double>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value());
    if (args.Length() == 4) argv[1] = Number::New(isolate, result);
    else args.GetReturnValue().Set(Number::New(isolate, result));

  } else if (!strcmp(dataType, "ptr") || !strcmp(dataType, "pointer")) {

    intptr_t result = Memory.readMemory<intptr_t>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value());
    if (args.Length() == 4) argv[1] = Number::New(isolate, result);
    else args.GetReturnValue().Set(Number::New(isolate, result));

  } else if (!strcmp(dataType, "bool") || !strcmp(dataType, "boolean")) {

    bool result = Memory.readMemory<bool>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value());
    if (args.Length() == 4) argv[1] = Boolean::New(isolate, result);
    else args.GetReturnValue().Set(Boolean::New(isolate, result));

  } else if (!strcmp(dataType, "string") || !strcmp(dataType, "str")) {

    std::vector<char> chars;
    int offset = 0x0;
    while (true) {
      char c = Memory.readMemoryChar((HANDLE)args[0]->Uint32Value(), args[1]->IntegerValue() + offset);
      chars.push_back(c);

      // break at 1 million chars
      if (offset == (sizeof(char) * 1000000)) {
        chars.clear();
        break;
      }

      // break at terminator
      if (c == '\0') {
        break;
      }

      offset += sizeof(char);
    }

    if (chars.size() == 0) {
    
      if (args.Length() == 4) argv[0] = String::NewFromUtf8(isolate, "unable to read string (no null-terminator found after 1 million chars)");
      else return memoryjs::throwError("unable to read string (no null-terminator found after 1 million chars)", isolate);
    
    } else {
      // vector -> string
      std::string str(chars.begin(), chars.end());

      if (args.Length() == 4) argv[1] = String::NewFromUtf8(isolate, str.c_str());
      else args.GetReturnValue().Set(String::NewFromUtf8(isolate, str.c_str()));
    
    }

  } else if (!strcmp(dataType, "vector3") || !strcmp(dataType, "vec3")) {

    Vector3 result = Memory.readMemory<Vector3>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value());
    Local<Object> moduleInfo = Object::New(isolate);
    moduleInfo->Set(String::NewFromUtf8(isolate, "x"), Number::New(isolate, result.x));
    moduleInfo->Set(String::NewFromUtf8(isolate, "y"), Number::New(isolate, result.y));
    moduleInfo->Set(String::NewFromUtf8(isolate, "z"), Number::New(isolate, result.z));

    if (args.Length() == 4) argv[1] = moduleInfo;
    else args.GetReturnValue().Set(moduleInfo);

  } else if (!strcmp(dataType, "vector4") || !strcmp(dataType, "vec4")) {
    
    Vector4 result = Memory.readMemory<Vector4>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value());
    Local<Object> moduleInfo = Object::New(isolate);
    moduleInfo->Set(String::NewFromUtf8(isolate, "w"), Number::New(isolate, result.w));
    moduleInfo->Set(String::NewFromUtf8(isolate, "x"), Number::New(isolate, result.x));
    moduleInfo->Set(String::NewFromUtf8(isolate, "y"), Number::New(isolate, result.y));
    moduleInfo->Set(String::NewFromUtf8(isolate, "z"), Number::New(isolate, result.z));

    if (args.Length() == 4) argv[1] = moduleInfo;
    else args.GetReturnValue().Set(moduleInfo);

  } else {

    if (args.Length() == 4) argv[0] = String::NewFromUtf8(isolate, "unexpected data type");
    else return memoryjs::throwError("unexpected data type", isolate);

  }

  // We check if there is three arguments and if the third argument is a function earlier on
  // now we check again if we must call the function passed on
  if (args.Length() == 4) callback->Call(Null(isolate), argc, argv);
}

void writeMemory(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();

  if (args.Length() != 4) {
    memoryjs::throwError("requires 4 arguments", isolate);
    return;
  }

  if (!args[0]->IsNumber() && !args[1]->IsNumber() && !args[3]->IsString()) {
    memoryjs::throwError("first and second argument must be a number, third argument must be a string", isolate);
    return;
  }

  v8::String::Utf8Value dataTypeArg(args[3]);
  char* dataType = (char*)*(dataTypeArg);

  // Set callback variables in the case the a callback parameter has been passed
  Local<Function> callback = Local<Function>::Cast(args[4]);
  const unsigned argc = 1;
  Local<Value> argv[argc];

  // Define the error message that will be set if no data type is recognised
  argv[0] = String::NewFromUtf8(isolate, "");

  // following if statements find the data type to read and then return the correct data type
  // args[0] -> Uint32Value() is the address to read, unsigned int is used because address needs to be positive
  // args[1] -> value is the value to write to the address
  if (!strcmp(dataType, "int")) {

    Memory.writeMemory<int>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), args[2]->NumberValue());

  } else if (!strcmp(dataType, "dword")) {

    Memory.writeMemory<DWORD>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), args[2]->NumberValue());

  } else if (!strcmp(dataType, "long")) {

    Memory.writeMemory<long>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), args[2]->NumberValue());

  } else if (!strcmp(dataType, "float")) {

    Memory.writeMemory<float>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), args[2]->NumberValue());

  } else if (!strcmp(dataType, "double")) {

    Memory.writeMemory<double>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), args[2]->NumberValue());

  } else if (!strcmp(dataType, "bool") || !strcmp(dataType, "boolean")) {

    Memory.writeMemory<bool>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), args[2]->BooleanValue());

  } else if (!strcmp(dataType, "string") || !strcmp(dataType, "str")) {

    v8::String::Utf8Value valueParam(args[2]->ToString());
    
    // Write String, Method 1
    //Memory.writeMemory<std::string>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), std::string(*valueParam));

    // Write String, Method 2
    Memory.writeMemory((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), *valueParam, valueParam.length());
    
  } else if (!strcmp(dataType, "vector3") || !strcmp(dataType, "vec3")) {

    Handle<Object> value = Handle<Object>::Cast(args[2]);
    Vector3 vector = {
      value->Get(String::NewFromUtf8(isolate, "x"))->NumberValue(),
      value->Get(String::NewFromUtf8(isolate, "y"))->NumberValue(),
      value->Get(String::NewFromUtf8(isolate, "z"))->NumberValue()
    };
    Memory.writeMemory<Vector3>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), vector);

  } else if (!strcmp(dataType, "vector4") || !strcmp(dataType, "vec4")) {

    Handle<Object> value = Handle<Object>::Cast(args[2]);
    Vector4 vector = {
      value->Get(String::NewFromUtf8(isolate, "w"))->NumberValue(),
      value->Get(String::NewFromUtf8(isolate, "x"))->NumberValue(),
      value->Get(String::NewFromUtf8(isolate, "y"))->NumberValue(),
      value->Get(String::NewFromUtf8(isolate, "z"))->NumberValue()
    };
    Memory.writeMemory<Vector4>((HANDLE)args[0]->Uint32Value(), args[1]->Uint32Value(), vector);

  } else {

    if (args.Length() == 5) argv[0] = String::NewFromUtf8(isolate, "unexpected data type");
    else return memoryjs::throwError("unexpected data type", isolate);

  }

  // If there is a callback, return the error message (blank if no error)
  if (args.Length() == 5) callback->Call(Null(isolate), argc, argv);
}

void findPattern(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();

  // if (args.Length() != 5 && args.Length() != 6) {
  //   memoryjs::throwError("requires 5 arguments, or 6 arguments if a callback is being used", isolate);
  //   return;
  // }

  // if (!args[0]->IsNumber() && !args[1]->IsString() && !args[2]->IsNumber() && !args[3]->IsNumber() && !args[4]->IsNumber()) {
  //   memoryjs::throwError("first argument must be a number, the remaining arguments must be numbers apart from the callback", isolate);
  //   return;
  // }

  // if (args.Length() == 6 && !args[5]->IsFunction()) {
  //   memoryjs::throwError("sixth argument must be a function", isolate);
  //   return;
  // }

  // Address of findPattern result
  uintptr_t address = -1;

  // Define error message that may be set by the function that gets the modules
  char* errorMessage = "";

  HANDLE handle = (HANDLE)args[0]->Uint32Value();

  std::vector<MODULEENTRY32> moduleEntries = Module.getModules(GetProcessId(handle), &errorMessage);

  // If an error message was returned from the function getting the modules, throw the error.
  // Only throw an error if there is no callback (if there's a callback, the error is passed there).
  if (strcmp(errorMessage, "") && args.Length() != 7) {
    memoryjs::throwError(errorMessage, isolate);
    return;
  }

  for (std::vector<MODULEENTRY32>::size_type i = 0; i != moduleEntries.size(); i++) {
    v8::String::Utf8Value moduleName(args[1]);

    if (!strcmp(moduleEntries[i].szModule, std::string(*moduleName).c_str())) {
      v8::String::Utf8Value signature(args[2]->ToString());
      address = Pattern.findPattern(handle, moduleEntries[i], std::string(*signature).c_str(), args[3]->Uint32Value(), args[4]->Uint32Value(), args[5]->Uint32Value());
      break;
    }
  }

  // If no error was set by getModules and the address is still the value we set it as, it probably means we couldn't find the module
  if (strcmp(errorMessage, "") && address == -1) errorMessage = "unable to find module";

  // If no error was set by getModules and the address is -2 this means there was no match to the pattern
  if (strcmp(errorMessage, "") && address == -2) errorMessage = "no match found";

  // findPattern can be asynchronous
  if (args.Length() == 7) {
    // Callback to let the user handle with the information
    Local<Function> callback = Local<Function>::Cast(args[5]);
    const unsigned argc = 2;
    Local<Value> argv[argc] = { String::NewFromUtf8(isolate, errorMessage), Number::New(isolate, address) };
    callback->Call(Null(isolate), argc, argv);
  } else {
    // return JSON
    args.GetReturnValue().Set(Number::New(isolate, address));
  }
}

void init(Local<Object> exports) {
  NODE_SET_METHOD(exports, "openProcess", openProcess);
  NODE_SET_METHOD(exports, "closeProcess", closeProcess);
  NODE_SET_METHOD(exports, "getProcesses", getProcesses);
  NODE_SET_METHOD(exports, "getModules", getModules);
  NODE_SET_METHOD(exports, "findModule", findModule);
  NODE_SET_METHOD(exports, "readMemory", readMemory);
  NODE_SET_METHOD(exports, "writeMemory", writeMemory);
  NODE_SET_METHOD(exports, "findPattern", findPattern);
}

NODE_MODULE(memoryjs, init)
