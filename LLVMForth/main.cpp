//
//  main.cpp
//  LLVMForth
//
//  Created by Sven Weidauer on 07.01.12.
//  Copyright (c) 2012 Bright Solutions. All rights reserved.
//

#include <stdint.h>

#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/PassManager.h"
#include "llvm/CallingConv.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/DerivedTypes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/TargetSelect.h"

#import <iostream>
#import <sstream>

void CallWord( const std::string &word );

struct Word {
	bool immediate;
	llvm::Function *function;
	
	void Execute();
	void Compile();
};

llvm::Module *module = NULL;
llvm::ExecutionEngine *engine = NULL;

void Word::Execute()
{
	assert( module != NULL );
	
	if (engine == NULL) {
		std::string errorString;
		engine = llvm::EngineBuilder( module ).setErrorStr( &errorString ).create();
		if (engine == NULL) {
			std::cerr << "Cannot create execution engine: " << errorString << std::endl;
			exit( 1 );
		}
	}
	
	void (*funcPtr)() = (void (*)())engine->getPointerToFunction( function );
	funcPtr();
}

llvm::IRBuilder<> *builder = NULL;

void Word::Compile()
{
	assert( builder != NULL );
	
	builder->CreateCall( function );
}

std::map<std::string,Word> dictionary;
bool executing = true;

void CallWord( const std::string &word )
{
	Word &def = dictionary[word];
	
	if (executing || def.immediate) {
		def.Execute();
	} else {
		def.Compile();
	}
}

intptr_t pop();
void push( intptr_t value );

extern "C" {
	void add();
	void add()
	{
		intptr_t a = pop();
		intptr_t b = pop();
		push( a + b );
	}
	
	void sub();
	void sub()
	{
		intptr_t a = pop();
		intptr_t b = pop();
		push( b - a );
	}
	
	void swap();
	void swap()
	{
		intptr_t a = pop();
		intptr_t b = pop();
		push( a );
		push( b );
	}
	
}

std::vector<intptr_t> stack;

intptr_t pop()
{
	if (stack.empty()) {
		std::cerr << "Stack underflow" << std::endl;
		exit( 1 );
	}
	
	intptr_t last = stack.back();
	stack.pop_back();
	return last;
}

void push( intptr_t value )
{
	stack.push_back( value );
}

extern "C" {
	void dump_stack();
	void dump_stack()
	{
		std::cout << "Stack: " << std::endl;
		for (auto it : stack) {
			std::cout << it << " ";
		}
		std::cout << std::endl << std::endl;
	}
}

void install_word( const char *word, bool immediate = false );
void install_word( const char *forthName, const char *name, bool immediate = false );

void install_word( const char *word, bool immediate )
{
	install_word( word, word, immediate );
}

void install_word( const char *forthName, const char *word, bool immediate  )
{
	llvm::FunctionType *ft = llvm::FunctionType::get( llvm::Type::getVoidTy( llvm::getGlobalContext() ), false );
	
	Word testWord;
	testWord.immediate = immediate;
	testWord.function = llvm::cast<llvm::Function>( module->getOrInsertFunction( word,  ft ) );
	
	dictionary[forthName] = testWord;
}


std::string word();
char key();

char key()
{
	int ch = getchar();
	if (ch == EOF) {
		exit( 0 );
	}
	
	return ch;
}

std::string word()
{
	char ch;
	std::string word;
	while ((ch = key()) != ' ') word += ch;
	return word;
}

void interpret();
extern "C" void literal();

void interpret()
{
	std::string w = word();
	
	auto it = dictionary.find( w );
	if (it != dictionary.end()) {
		CallWord( w );
	} else {
		std::istringstream stream( w );
		intptr_t value = 0;
		stream >> value;
		
		push( value );
		if (!executing) literal();
	}
}

Word currentWord;
std::string currentName;

extern "C" {
	
	void docol();
	void docol() 
	{
		executing = false;
		std::string name = word();

		llvm::FunctionType *ft = llvm::FunctionType::get( llvm::Type::getVoidTy( llvm::getGlobalContext() ), false );

		llvm::Function *function = llvm::cast<llvm::Function>( module->getOrInsertFunction( name, ft ) );
		llvm::BasicBlock *block = llvm::BasicBlock::Create( llvm::getGlobalContext(), "entry", function );
		
		builder = new llvm::IRBuilder<>( block );
		
		currentWord.immediate = false;
		currentWord.function = function;
		currentName = name;
	}
	
	void done();
	void done()
	{
		executing = true;
		builder->CreateRetVoid();
		delete builder;
		builder = NULL;
		
		dictionary[currentName] = currentWord;
		
		module->dump();
	}
	
	void immediate();
	void immediate()
	{
		currentWord.immediate = true;
	}

	void lit( intptr_t value );
	void lit( intptr_t value )
	{
		push( value );
	}

	void literal()
	{
		intptr_t value = pop();
		llvm::Constant *litFunc = module->getOrInsertFunction( "lit", llvm::Type::getVoidTy( llvm::getGlobalContext() ), llvm::Type::getInt64Ty( llvm::getGlobalContext() ), NULL );
		llvm::Constant *constant = llvm::Constant::getIntegerValue( llvm::Type::getInt64Ty( llvm::getGlobalContext() ), llvm::APInt( 64, value, true ) );
		
		builder->CreateCall( litFunc, constant );
	}
	
	void lbrac();
	void lbrac() { executing = true; }
	void rbrac();
	void rbrac() { executing = false; }
	
	void dochar();
	void dochar()
	{
		std::string n = word();
		push( n[0] );
	}
	
	void drop();
	void drop() { pop(); }
	
	void forth_dup();
	void forth_dup()
	{
		intptr_t a = pop();
		push( a );
		push( a );
	}
	
	void load();
	void load()
	{
		intptr_t addr = pop();
		push( *(intptr_t *)addr );
	}
	
	void store();
	void store()
	{
		intptr_t addr = pop();
		intptr_t value = pop();
		*(intptr_t *)addr = value;
	}
}



int main (int argc, const char * argv[])
{
	llvm::InitializeNativeTarget();
	
	llvm::LLVMContext &Context = llvm::getGlobalContext();
	
	module = new llvm::Module( "forth", Context );

	install_word( "dump_stack" );
	install_word( "+", "add" );
	install_word( "-", "sub" );
	install_word( "swap" );
	install_word( "drop" );
	install_word( "dup", "forth_dup" );
	install_word( ":", "docol" );
	install_word( ";", "done", true );
	install_word( "[", "lbrac", true );
	install_word( "]", "rbrac" );
	install_word( "char", "dochar" );
	install_word( "literal", true );
	install_word( "@", "load" );
	install_word( "!", "store" );
	install_word( "immediate", true );
	
	for (;;) interpret();
	
    return 0;
}

