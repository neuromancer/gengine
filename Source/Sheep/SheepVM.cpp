//
// SheepVM.cpp
//
// Clark Kromenaker
//
#include "SheepVM.h"

#include <iostream>

#include "BinaryReader.h"
#include "GMath.h"
#include "SheepAPI.h"
#include "SheepScript.h"
#include "Services.h"
#include "StringUtil.h"

//#define SHEEP_DEBUG

std::string SheepInstance::GetName()
{
	if(mSheepScript != nullptr)
	{
		return mSheepScript->GetNameNoExtension();
	}
	return "";
}

SheepVM::~SheepVM()
{
	for(auto& instance : mSheepInstances)
	{
		delete instance;
	}
	for(auto& thread : mSheepThreads)
	{
		delete thread;
	}
}

void SheepVM::Execute(SheepScript* script, std::function<void()> finishCallback)
{
	// Just default to zero offset (aka the first function in the script).
	Execute(script, 0, finishCallback);
}

void SheepVM::Execute(SheepScript* script, const std::string& functionName, std::function<void()> finishCallback)
{
	// We need a valid script.
	if(script == nullptr)
	{
		if(finishCallback != nullptr)
		{
			finishCallback();
		}
		return;
	}
	
	// Get bytecode offset for this function. If less than zero,
	// it means the function doesn't exist, and we've got to fail out.
	int bytecodeOffset = script->GetFunctionOffset(functionName);
	if(bytecodeOffset < 0)
	{
		std::cout << "ERROR: Couldn't find function: " << functionName << std::endl;
		if(finishCallback != nullptr)
		{
			finishCallback();
		}
		return;
	}
	
	// Execute at bytecode offset.
	ExecuteInternal(script, bytecodeOffset, functionName, finishCallback);
}

void SheepVM::Execute(SheepScript* script, int bytecodeOffset, std::function<void()> finishCallback)
{
	ExecuteInternal(script, bytecodeOffset, "X$", finishCallback);
}

bool SheepVM::Evaluate(SheepScript* script, int n, int v)
{
	// Get an execution context.
	SheepInstance* instance = GetInstance(script);
	
	// For NVC evaluation logic, scripts can use built-in variables $n and $v.
	// These variables refer to whatever the current noun and current verb are, using an int identifier.
	// Pass these values in, but only if the context can support them.
	if(instance->mVariables.size() > 0 && instance->mVariables[0].type == SheepValueType::Int)
	{
		instance->mVariables[0].intValue = n;
	}
	if(instance->mVariables.size() > 1 && instance->mVariables[1].type == SheepValueType::Int)
	{
		instance->mVariables[1].intValue = v;
	}
	
	//std::cout << "SHEEP EVALUATE START - Stack size is " << mStackSize << std::endl;
    // Execute the script, per usual.
    SheepThread* thread = ExecuteInternal(instance, 0, "X$", nullptr);
    
    // If stack is empty, return false.
	if(thread->mStack.Size() == 0) { return false; }
	
    // Check the top item on the stack and return true or false based on that.
	SheepValue& result = thread->mStack.Pop();
    if(result.type == SheepValueType::Int)
    {
        return result.intValue != 0;
    }
    else if(result.type == SheepValueType::Float)
    {
        return !Math::AreEqual(result.floatValue, 0.0f);
    }
    else if(result.type == SheepValueType::String)
    {
        std::string str(result.stringValue);
        return !str.empty();
    }

    // Default to false.
    return false;
}

bool SheepVM::IsAnyRunning() const
{
	for(auto& thread : mSheepThreads)
	{
		if(thread->mRunning) { return true; }
	}
	return false;
}

SheepInstance* SheepVM::GetInstance(SheepScript* script)
{
	// Don't create without a valid script.
	if(script == nullptr) { return nullptr; }
	
	// If an instance already exists for this sheep, just reuse that one.
	// This *might* be important b/c we want variables in the same script to be shared.
	// Ex: call IncCounter$ in same sheep, the counter variable should still be incremented after returning.
	for(auto& instance : mSheepInstances)
	{
		if(instance->mSheepScript == script)
		{
			return instance;
		}
	}
	
	// Try to reuse an execution context that is no longer being used.
	SheepInstance* context = nullptr;
	for(auto& instance : mSheepInstances)
	{
		if(instance->mReferenceCount == 0)
		{
			context = instance;
			break;
		}
	}
	
	// Create a new instance if we have to.
	if(context == nullptr)
	{
		context = new SheepInstance();
		mSheepInstances.push_back(context);
	}
	context->mSheepScript = script;
	
	// Create copy of variables for assignment during execution.
	context->mVariables = script->GetVariables();
	return context;
}

SheepThread* SheepVM::GetThread()
{
	// Recycle a previously used thread, if possible.
	SheepThread* useThread = nullptr;
	for(auto& thread : mSheepThreads)
	{
		if(!thread->mRunning)
		{
			useThread = thread;
			break;
		}
	}
	
	// If needed, create a new thread instead.
	if(useThread == nullptr)
	{
		useThread = new SheepThread();
		useThread->mVirtualMachine = this;
		mSheepThreads.push_back(useThread);
	}
	return useThread;
}

Value SheepVM::CallSysFunc(SheepThread* thread, SysImport* sysImport)
{
	// Retrieve system function declaration for the system function import.
	// We need the full declaration to know whether this is a waitable function!
	SysFuncDecl* sysFunc = GetSysFuncDecl(sysImport);
	if(sysFunc == nullptr)
	{
		std::cout << "Sheep uses undeclared function " << sysImport->name << std::endl;
		return Value(0);
	}
	
	// Number on top of stack is argument count.
	// Make sure it matches the argument count from the system function declaration.
	int argCount = thread->mStack.Pop().intValue;
	assert(argCount == sysFunc->argumentTypes.size());
	
	// Retrieve the arguments, of the expected types, from the stack.
	std::vector<Value> args;
	for(int i = 0; i < argCount; i++)
	{
		SheepValue& sheepValue = thread->mStack.Peek(argCount - 1 - i);
		//SheepValue sheepValue = mStack[mStackSize - (argCount - i)];
		int argType = sysFunc->argumentTypes[i];
		switch(argType)
		{
		case 1:
			args.push_back(sheepValue.GetInt());
			break;
		case 2:
			args.push_back(sheepValue.GetFloat());
			break;
		case 3:
			args.push_back(sheepValue.GetString());
			break;
		default:
			std::cout << "Invalid arg type: " << argType << std::endl;
			break;
		}
	}
	thread->mStack.Pop(argCount);
	
	/*
	{
		// Pretty useful for seeing the function that was called output to the console.
		std::cout << "SysFunc " << sysFunc->name << "(";
		for(int i = 0; i < argCount; i++)
		{
			switch(sysFunc->argumentTypes[i])
			{
			case 1:
				std::cout << args[i].to<int>();
				break;
			case 2:
				std::cout << args[i].to<float>();
				break;
			case 3:
				std::cout << args[i].to<std::string>();
				break;
			}

			if(i < argCount - 1)
			{
				std::cout << ", ";
			}
		}
		std::cout << ")" << std::endl;
	}
	*/
	
	// Based on argument count, call the appropriate function variant.
	Value v = Value(0);
	switch(argCount)
	{
	case 0:
		v = ::CallSysFunc(sysFunc->name);
		break;
	case 1:
		v = ::CallSysFunc(sysFunc->name, args[0]);
		break;
	case 2:
		v = ::CallSysFunc(sysFunc->name, args[0], args[1]);
		break;
	case 3:
		v = ::CallSysFunc(sysFunc->name, args[0], args[1], args[2]);
		break;
	case 4:
		v = ::CallSysFunc(sysFunc->name, args[0], args[1], args[2], args[3]);
		break;
	case 5:
		v = ::CallSysFunc(sysFunc->name, args[0], args[1], args[2], args[3], args[4]);
		break;
	case 6:
		v = ::CallSysFunc(sysFunc->name, args[0], args[1], args[2], args[3], args[4], args[5]);
		break;
	default:
		std::cout << "SheepVM: Unimplemented arg count: " << argCount << std::endl;
		break;
	}
	
	// Output a general execution exception if we encountered a problem in the sys func call.
	if(mExecutionError)
	{
		Services::GetReports()->Log("Error", StringUtil::Format("An error occurred while executing %s", thread->GetName().c_str()));
		mExecutionError = false;
	}
	
	// Return result of sys func call.
	return v;
}

SheepThread* SheepVM::ExecuteInternal(SheepScript *script, int bytecodeOffset,
        const std::string &functionName, std::function<void ()> finishCallback)
{
	return ExecuteInternal(GetInstance(script), bytecodeOffset, functionName, finishCallback);
}

SheepThread* SheepVM::ExecuteInternal(SheepInstance* instance, int bytecodeOffset,
									  const std::string& functionName, std::function<void()> finishCallback)
{
	// A valid execution context is required.
	if(instance == nullptr)
	{
		if(finishCallback != nullptr)
		{
			finishCallback();
		}
		return nullptr;
	}
	
	// Create a sheep thread to perform the execution.
	SheepThread* thread = GetThread();
	thread->mContext = instance;
	thread->mWaitCallback = finishCallback;
	thread->mCodeOffset = bytecodeOffset;
	
	// Save name and start offset (for debugging/info).
	thread->mFunctionName = functionName;
	thread->mFunctionStartOffset = bytecodeOffset;
	
	// The thread is using this execution context.
	instance->mReferenceCount++;
	
	// Start the thread of execution.
	ExecuteInternal(thread);
	return thread;
}

void SheepVM::ExecuteInternal(SheepThread* thread)
{
	// Store previous thread and set passed in thead as the currently executing thread.
	SheepThread* prevThread = mCurrentThread;
	mCurrentThread = thread;
	
	// Sheep is either being created/started, or was released from a wait block.
	if(!thread->mRunning)
	{
		thread->mRunning = true;
		Services::GetReports()->Log("SheepMachine", "Sheep " + thread->GetName() + " created and starting");
	}
	else if(thread->mInWaitBlock)
	{
		thread->mBlocked = false;
		thread->mInWaitBlock = false;
		Services::GetReports()->Log("SheepMachine", "Sheep " + thread->GetName() + " released at line -1");
	}
	
	// Get instance/script we'll be using.
	SheepInstance* instance = thread->mContext;
	SheepScript* script = instance->mSheepScript;
	
    // Get bytecode and generate a binary reader for easier parsing.
    char* bytecode = script->GetBytecode();
    int bytecodeLength = script->GetBytecodeLength();
    
    // Create reader for the bytecode.
    BinaryReader reader(bytecode, bytecodeLength);
    if(!reader.OK()) { return; }
    
    // Skip ahead to desired offset.
    reader.Skip(thread->mCodeOffset);
    
    // Read each byte in turn, interpret and execute the instruction.
	bool stopReading = false;
	while(!stopReading)
    {
		// Read instruction.
        char instruction = reader.ReadUByte();
		
		// Break when read instruction fails (perhaps due to reading past end of file/mem stream).
		if(!reader.OK()) { break; }
		
		// Perform the action associated with each instruction.
        switch((SheepInstruction)instruction)
        {
            case SheepInstruction::SitnSpin:
            {
                // No-op; do nothing.
				#ifdef SHEEP_DEBUG
				std::cout << "SitnSpin" << std::endl;
				#endif
                break;
            }
            case SheepInstruction::Yield:
            {
                // Not totally sure what this instruction does.
				// Maybe it yields sheep execution until next frame?
				#ifdef SHEEP_DEBUG
				std::cout << "Yield" << std::endl;
				#endif
				stopReading = true;
                break;
            }
            case SheepInstruction::CallSysFunctionV:
            {
                int functionIndex = reader.ReadInt();
                SysImport* sysFunc = script->GetSysImport(functionIndex);
				if(sysFunc == nullptr)
				{
					std::cout << "Invalid function index " << functionIndex << std::endl;
					break;
				}
				
				#ifdef SHEEP_DEBUG
				std::cout << "CallSysFuncV " << sysFunc->name << std::endl;
				#endif
				
				// Execute the system function.
                Value value = CallSysFunc(thread, sysFunc);
				
				// Though this is void return, we still push type of "shpvoid" onto stack.
				// The compiler generates an extra "Pop" instruction after a CallSysFunctionV.
				// This matches how the original game's compiler generated instructions!
				thread->mStack.PushInt(value.to<shpvoid>());
                break;
            }
            case SheepInstruction::CallSysFunctionI:
            {
                int functionIndex = reader.ReadInt();
                SysImport* sysFunc = script->GetSysImport(functionIndex);
				if(sysFunc == nullptr)
				{
					std::cout << "Invalid function index " << functionIndex << std::endl;
					break;
				}
				
				#ifdef SHEEP_DEBUG
				std::cout << "CallSysFuncI " << sysFunc->name << std::endl;
				#endif
				
				// Execute the system function.
                Value value = CallSysFunc(thread, sysFunc);
				
				// Push the int result onto the stack.
				thread->mStack.PushInt(value.to<int>());
                break;
            }
            case SheepInstruction::CallSysFunctionF:
            {
                int functionIndex = reader.ReadInt();
                SysImport* sysFunc = script->GetSysImport(functionIndex);
				if(sysFunc == nullptr)
				{
					std::cout << "Invalid function index " << functionIndex << std::endl;
					break;
				}
				
				#ifdef SHEEP_DEBUG
				std::cout << "CallSysFuncF " << sysFunc->name << std::endl;
				#endif
				
				// Execute the system function.
                Value value = CallSysFunc(thread, sysFunc);
				
				// Push the float result onto the stack.
				thread->mStack.PushFloat(value.to<float>());
                break;
            }
            case SheepInstruction::CallSysFunctionS:
            {
                int functionIndex = reader.ReadInt();
                SysImport* sysFunc = script->GetSysImport(functionIndex);
				if(sysFunc == nullptr)
				{
					std::cout << "Invalid function index " << functionIndex << std::endl;
					break;
				}
				
				#ifdef SHEEP_DEBUG
				std::cout << "CallSysFuncS " << sysFunc->name << std::endl;
				#endif
				
				// Execute the system function.
                Value value = CallSysFunc(thread, sysFunc);
				
				// Push the string result onto the stack.
				thread->mStack.PushString(value.to<std::string>().c_str()); //TODO: Seems like this could cause problems? Where is value's string coming from? What if it is deallocated???
                break;
            }
            case SheepInstruction::Branch:
            {
				#ifdef SHEEP_DEBUG
				std::cout << "Branch" << std::endl;
				#endif
				int branchAddress = reader.ReadInt();
				reader.Seek(branchAddress);
                break;
            }
            case SheepInstruction::BranchGoto:
            {
				#ifdef SHEEP_DEBUG
				std::cout << "BranchGoto" << std::endl;
				#endif
				int branchAddress = reader.ReadInt();
				reader.Seek(branchAddress);
                break;
            }
            case SheepInstruction::BranchIfZero:
            {
				// Regardless of whether we do branch, we need to pull
				// the branch address from the reader.
				int branchAddress = reader.ReadInt();
				
				#ifdef SHEEP_DEBUG
				std::cout << "BranchIfZero" << std::endl;
				#endif
				
				// If top item on stack is zero, we will branch.
				// This operation also pops off the stack.
				SheepValue& result = thread->mStack.Pop();
				if(result.intValue == 0)
				{
					reader.Seek(branchAddress);
				}
                break;
            }
            case SheepInstruction::BeginWait:
            {
				#ifdef SHEEP_DEBUG
				std::cout << "BeginWait" << std::endl;
				#endif
				thread->mInWaitBlock = true;
                break;
            }
            case SheepInstruction::EndWait:
            {
				#ifdef SHEEP_DEBUG
				std::cout << "EndWait " << thread->mInWaitBlock << ", " << thread->mWaitCounter << std::endl;
				#endif
				// If waiting on one or more WAIT-able functions, we need to STOP thread execution for now!
				// We will resume this thread's execution once we get enough wait callbacks.
				if(thread->mWaitCounter > 0)
				{
					thread->mBlocked = true;
					stopReading = true;
				}
				else
				{
					thread->mInWaitBlock = false;
				}
                break;
            }
            case SheepInstruction::ReturnV:
            {
                // This means we've reached the end of the executing function.
                // So, we just return to the caller, for realz.
				#ifdef SHEEP_DEBUG
				std::cout << "ReturnV" << std::endl;
				#endif
				thread->mRunning = false;
				stopReading = true;
				break;
            }
            case SheepInstruction::StoreI:
            {
                int varIndex = reader.ReadInt();
                if(varIndex >= 0 && varIndex < instance->mVariables.size())
                {
					#ifdef SHEEP_DEBUG
					std::cout << "StoreI " << thread->mStack.Peek(0).intValue << std::endl;
					#endif
					
                    assert(instance->mVariables[varIndex].type == SheepValueType::Int);
					SheepValue& value = thread->mStack.Pop();
					instance->mVariables[varIndex].intValue = value.intValue;
                }
                break;
            }
            case SheepInstruction::StoreF:
            {
                int varIndex = reader.ReadInt();
                if(varIndex >= 0 && varIndex < instance->mVariables.size())
                {
					#ifdef SHEEP_DEBUG
					std::cout << "StoreF " << thread->mStack.Peek(0).floatValue << std::endl;
					#endif
					
                    assert(instance->mVariables[varIndex].type == SheepValueType::Float);
					SheepValue& value = thread->mStack.Pop();
                    instance->mVariables[varIndex].floatValue = value.floatValue;
                }
                break;
            }
            case SheepInstruction::StoreS:
            {
                int varIndex = reader.ReadInt();
                if(varIndex >= 0 && varIndex < instance->mVariables.size())
                {
					#ifdef SHEEP_DEBUG
					std::cout << "StoreS " << thread->mStack.Peek(0).stringValue << std::endl;
					#endif
					
                    assert(instance->mVariables[varIndex].type == SheepValueType::String);
					SheepValue& value = thread->mStack.Pop();
                    instance->mVariables[varIndex].stringValue = value.stringValue;
                }
                break;
            }
            case SheepInstruction::LoadI:
            {
                int varIndex = reader.ReadInt();
                if(varIndex >= 0 && varIndex < instance->mVariables.size())
                {
					#ifdef SHEEP_DEBUG
					std::cout << "LoadI " << instance->mVariables[varIndex].intValue << std::endl;
					#endif
					
                    assert(instance->mVariables[varIndex].type == SheepValueType::Int);
					thread->mStack.PushInt(instance->mVariables[varIndex].intValue);
                }
                break;
            }
            case SheepInstruction::LoadF:
            {
                int varIndex = reader.ReadInt();
                if(varIndex >= 0 && varIndex < instance->mVariables.size())
                {
					#ifdef SHEEP_DEBUG
					std::cout << "LoadF " << instance->mVariables[varIndex].floatValue << std::endl;
					#endif
					
                    assert(instance->mVariables[varIndex].type == SheepValueType::Float);
					thread->mStack.PushFloat(instance->mVariables[varIndex].floatValue);
                }
                break;
            }
            case SheepInstruction::LoadS:
            {
                int varIndex = reader.ReadInt();
                if(varIndex >= 0 && varIndex < instance->mVariables.size())
                {
					#ifdef SHEEP_DEBUG
					std::cout << "LoadS " << instance->mVariables[varIndex].stringValue << std::endl;
					#endif
					
                    assert(instance->mVariables[varIndex].type == SheepValueType::String);
					thread->mStack.PushString(instance->mVariables[varIndex].stringValue);
                }
                break;
            }
            case SheepInstruction::PushI:
            {
                int int1 = reader.ReadInt();
				#ifdef SHEEP_DEBUG
				std::cout << "PushI " << int1 << std::endl;
				#endif
				thread->mStack.PushInt(int1);
                break;
            }
            case SheepInstruction::PushF:
            {
                float float1 = reader.ReadFloat();
				#ifdef SHEEP_DEBUG
				std::cout << "PushF " << float1 << std::endl;
				#endif
				thread->mStack.PushFloat(float1);
                break;
            }
            case SheepInstruction::PushS:
            {
                int stringConstOffset = reader.ReadInt();
				#ifdef SHEEP_DEBUG
				std::cout << "PushS " << stringConstOffset << std::endl;
				#endif
				thread->mStack.PushStringOffset(stringConstOffset);
                break;
            }
			case SheepInstruction::GetString:
			{
				SheepValue& offsetValue = thread->mStack.Pop();
				std::string* stringPtr = script->GetStringConst(offsetValue.intValue);
				if(stringPtr != nullptr)
				{
					thread->mStack.PushString(stringPtr->c_str());
				}
				#ifdef SHEEP_DEBUG
				std::cout << "GetString " << thread->mStack.Peek().stringValue << std::endl;
				#endif
				break;
			}
            case SheepInstruction::Pop:
            {
				#ifdef SHEEP_DEBUG
				std::cout << "Pop" << std::endl;
				#endif
				thread->mStack.Pop(1);
                break;
            }
            case SheepInstruction::AddI:
            {
                assert(thread->mStack.Size() >= 2);
				int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "AddI " << int1 << " + " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 + int2);
                break;
            }
            case SheepInstruction::AddF:
            {
				assert(thread->mStack.Size() >= 2);
                float float1 = thread->mStack.Peek(1).floatValue;
                float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "AddF " << float1 << " + " << float2 << std::endl;
				#endif
				thread->mStack.PushFloat(float1 + float2);
                break;
            }
            case SheepInstruction::SubtractI:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "SubtractI " << int1 << " - " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 - int2);
                break;
            }
            case SheepInstruction::SubtractF:
            {
                assert(thread->mStack.Size() >= 2);
                float float1 = thread->mStack.Peek(1).floatValue;
                float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "SubtractF " << float1 << " - " << float2 << std::endl;
				#endif
				thread->mStack.PushFloat(float1 - float2);
                break;
            }
            case SheepInstruction::MultiplyI:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "MultiplyI " << int1 << " * " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 * int2);
                break;
            }
            case SheepInstruction::MultiplyF:
            {
                assert(thread->mStack.Size() >= 2);
                float float1 = thread->mStack.Peek(1).floatValue;
                float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "MultiplyF " << float1 << " * " << float2 << std::endl;
				#endif
				thread->mStack.PushFloat(float1 * float2);
                break;
            }
            case SheepInstruction::DivideI:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "DivideI " << int1 << " / " << int2 << std::endl;
				#endif
				// If dividing by zero, we'll spit out an error and just put a zero on the stack.
				if(int2 != 0)
				{
					thread->mStack.PushInt(int1 / int2);
				}
				else
				{
					std::cout << "Divide by zero!" << std::endl;
					thread->mStack.PushInt(0);
				}
                break;
            }
            case SheepInstruction::DivideF:
            {
                assert(thread->mStack.Size() >= 2);
                float float1 = thread->mStack.Peek(1).floatValue;
                float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "DivideF " << float1 << " / " << float2 << std::endl;
				#endif
				// If dividing by zero, we'll spit out an error and just put a zero on the stack.
				if(!Math::AreEqual(float2, 0.0f))
				{
					thread->mStack.PushFloat(float1 / float2);
				}
				else
				{
					std::cout << "Divide by zero!" << std::endl;
					thread->mStack.PushFloat(0.0f);
				}
                break;
            }
            case SheepInstruction::NegateI:
            {
                assert(thread->mStack.Size() >= 1);
				
				#ifdef SHEEP_DEBUG
				std::cout << "NegateI " << thread->mStack.Peek(0).intValue << std::endl;
				#endif
                thread->mStack.Peek(0).intValue *= -1;
                break;
            }
            case SheepInstruction::NegateF:
            {
                assert(thread->mStack.Size() >= 1);
				
				#ifdef SHEEP_DEBUG
				std::cout << "NegateF " << thread->mStack.Peek(0).floatValue << std::endl;
				#endif
                thread->mStack.Peek(0).floatValue *= -1.0f;
                break;
            }
            case SheepInstruction::IsEqualI:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsEqualI " << int1 << " == " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 == int2 ? 1 : 0);
                break;
            }
            case SheepInstruction::IsEqualF:
            {
                assert(thread->mStack.Size() >= 2);
                float float1 = thread->mStack.Peek(1).floatValue;
                float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsEqualF " << float1 << " == " << float2 << std::endl;
				#endif
				thread->mStack.PushInt(Math::AreEqual(float1, float2) ? 1 : 0);
                break;
            }
            case SheepInstruction::IsNotEqualI:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsNotEqualI " << int1 << " != " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 != int2 ? 1 : 0);
                break;
            }
            case SheepInstruction::IsNotEqualF:
            {
                assert(thread->mStack.Size() >= 2);
                float float1 = thread->mStack.Peek(1).floatValue;
                float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsNotEqualF " << float1 << " != " << float2 << std::endl;
				#endif
				thread->mStack.PushInt(!Math::AreEqual(float1, float2) ? 1 : 0);
                break;
            }
            case SheepInstruction::IsGreaterI:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsGreaterI " << int1 << " > " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 > int2 ? 1 : 0);
                break;
            }
            case SheepInstruction::IsGreaterF:
            {
                assert(thread->mStack.Size() >= 2);
                float float1 = thread->mStack.Peek(1).floatValue;
                float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsGreaterF " << float1 << " > " << float2 << std::endl;
				#endif
				thread->mStack.PushInt(float1 > float2 ? 1 : 0);
                break;
            }
			case SheepInstruction::IsLessI:
			{
				assert(thread->mStack.Size() >= 2);
				int int1 = thread->mStack.Peek(1).intValue;
				int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsLessI " << int1 << " < " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 < int2 ? 1 : 0);
				break;
			}
			case SheepInstruction::IsLessF:
			{
				assert(thread->mStack.Size() >= 2);
				float float1 = thread->mStack.Peek(1).floatValue;
				float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsLessF " << float1 << " < " << float2 << std::endl;
				#endif
				thread->mStack.PushInt(float1 < float2 ? 1 : 0);
				break;
			}
            case SheepInstruction::IsGreaterEqualI:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsGreaterEqualI " << int1 << " >= " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 >= int2 ? 1 : 0);
                break;
            }
            case SheepInstruction::IsGreaterEqualF:
            {
                assert(thread->mStack.Size() >= 2);
                float float1 = thread->mStack.Peek(1).floatValue;
                float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsGreaterEqualF " << float1 << " >= " << float2 << std::endl;
				#endif
				thread->mStack.PushInt(float1 >= float2 ? 1 : 0);
                break;
            }
            case SheepInstruction::IsLessEqualI:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsLessEqualI " << int1 << " <= " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 <= int2 ? 1 : 0);
                break;
            }
            case SheepInstruction::IsLessEqualF:
            {
                assert(thread->mStack.Size() >= 2);
                float float1 = thread->mStack.Peek(1).floatValue;
                float float2 = thread->mStack.Peek(0).floatValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IsLessEqualF " << float1 << " <= " << float2 << std::endl;
				#endif
				thread->mStack.PushInt(float1 <= float2 ? 1 : 0);
                break;
            }
            case SheepInstruction::IToF:
            {
                int index = reader.ReadInt();
				SheepValue& value = thread->mStack.Peek(index);
				
				#ifdef SHEEP_DEBUG
				std::cout << "IToF " << value.intValue << std::endl;
				#endif
                value.floatValue = value.intValue;
                value.type = SheepValueType::Float;
                break;
            }
            case SheepInstruction::FToI:
            {
                int index = reader.ReadInt();
				SheepValue& value = thread->mStack.Peek(index);
				
				#ifdef SHEEP_DEBUG
				std::cout << "FToI " << value.floatValue << std::endl;
				#endif
                value.intValue = value.floatValue;
                value.type = SheepValueType::Int;
                break;
            }
            case SheepInstruction::Modulo:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "Modulo " << int1 << " % " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 % int2);
                break;
            }
            case SheepInstruction::And:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "And " << int1 << " && " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 && int2 ? 1 : 0);
                break;
            }
            case SheepInstruction::Or:
            {
                assert(thread->mStack.Size() >= 2);
                int int1 = thread->mStack.Peek(1).intValue;
                int int2 = thread->mStack.Peek(0).intValue;
				thread->mStack.Pop(2);
				
				#ifdef SHEEP_DEBUG
				std::cout << "Or " << int1 << " || " << int2 << std::endl;
				#endif
				thread->mStack.PushInt(int1 || int2 ? 1 : 0);
                break;
            }
            case SheepInstruction::Not:
            {
                assert(thread->mStack.Size() >= 1);
				int int1 = thread->mStack.Peek(0).intValue;
				
				#ifdef SHEEP_DEBUG
				std::cout << "Not " << int1 << std::endl;
				#endif
                thread->mStack.Peek(0).intValue = (int1 == 0 ? 1 : 0);
                break;
            }
            case SheepInstruction::DebugBreakpoint:
            {
				#ifdef SHEEP_DEBUG
				std::cout << "DebugBreakpoint" << std::endl;
				#endif
				//TODO: Break in Xcode/VS.
                break;
            }
            default:
            {
				std::cout << "Unaccounted for Sheep Instruction: " << (int)instruction << std::endl;
                break;
            }
        }
    }
	
	// Update thread's code offset value.
	thread->mCodeOffset = reader.GetPosition();
	
	// If reached end of file, assume the thread is no longer running.
	if(!reader.OK())
	{
		thread->mRunning = false;
	}
	
	// If thread is no longer running, notify anyone who was waiting for the thread to finish.
	// If we get here and the thread IS running, it means the thread was blocked due to a wait!
	if(!thread->mRunning)
	{
		Services::GetReports()->Log("SheepMachine", "Sheep " + thread->GetName() + " is exiting");
		
		// Thread is no longer using execution context.
		thread->mContext->mReferenceCount--;
		
		// Call my wait callback - someone might have been waiting for this thread to finish.
		if(thread->mWaitCallback)
		{
			thread->mWaitCallback();
		}
	}
	else if(thread->mInWaitBlock)
	{
		Services::GetReports()->Log("SheepMachine", "Sheep " + thread->GetName() + " is blocked at line -1");
	}
	else
	{
		Services::GetReports()->Log("SheepMachine", "Sheep " + thread->GetName() + " is in some weird unexpected state!");
	}
	
	// Restore previously executing thread.
	mCurrentThread = prevThread;
}
