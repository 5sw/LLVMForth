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

inline __attribute__((always_inline)) intptr_t pop() 
{
	if (stack.empty()) {
		std::cerr << "Stack underflow" << std::endl;
		exit( 1 );
	}
	
	intptr_t last = stack.back();
	stack.pop_back();
	return last;
}

inline __attribute__((always_inline))  void push( intptr_t value )
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

llvm::Type *WordType();

llvm::Type *WordType()
{
	static llvm::Type *wordType = NULL;
	if (wordType == NULL) {
		wordType = llvm::Type::getInt64Ty( llvm::getGlobalContext() );
	}
	return wordType;
}


llvm::Value *cpop();

llvm::Value *cpop()
{
	static llvm::Constant *getFunc = NULL;
	if (getFunc == NULL) {
		getFunc = module->getOrInsertFunction( "get", WordType(), NULL );
	}
	return builder->CreateCall( getFunc );
}

void cpush( llvm::Value *value );

void cpush( llvm::Value *value )
{
	static llvm::Constant *litFunc = NULL;
	if (litFunc == NULL) {
		litFunc = module->getOrInsertFunction( "lit", llvm::Type::getVoidTy( llvm::getGlobalContext() ), WordType(), NULL );
	}
	builder->CreateCall( litFunc, value );
}

llvm::FunctionPassManager *fpm = NULL;

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
		builder->CreateRetVoid();
		
		llvm::Function *func = builder->GetInsertBlock()->getParent();

		delete builder;
		builder = NULL;

		
		if (fpm == NULL) {
			fpm = new llvm::FunctionPassManager( module );
			fpm->add( new llvm::TargetData( *engine->getTargetData() ) );
			fpm->add( llvm::createBasicAliasAnalysisPass() );
			fpm->add( llvm::createInstructionCombiningPass() );
			fpm->add( llvm::createReassociatePass() );
			fpm->add( llvm::createGVNPass() );
			fpm->add( llvm::createCFGSimplificationPass() );
			fpm->doInitialization();
		}
		
		fpm->run( *func );

		func->dump();
		llvm::verifyFunction( *func );

		dictionary[currentName] = currentWord;
		executing = true;
		
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

	intptr_t get();
	intptr_t get()
	{
		return pop();
	}

	void literal()
	{
		if (executing) return;
		
		intptr_t value = pop();
		
		cpush( builder->getInt64( value ) );
		//		cpush( llvm::Constant::getIntegerValue( WordType(), llvm::APInt( 64, value, true ) ) );
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
	
	void cadd();
	void cadd()
	{
		if (executing) {
			push( pop() + pop() );
		} else {
			llvm::Value *a = cpop();
			llvm::Value *b = cpop();
			cpush( builder->CreateBinOp( llvm::Instruction::Add, a, b ) );
		}
	}
	
	void csub();
	void csub()
	{
		if (executing) {
			push( pop() - pop() );
		} else {
			llvm::Value *a = cpop();
			llvm::Value *b = cpop();
			cpush( builder->CreateBinOp( llvm::Instruction::Sub, a, b ) );
		}
	}
	
	void cdup();
	void cdup()
	{
		if (executing) {
			push( stack.back() );
		} else {
			auto val = cpop();
			cpush( val );
			cpush( val );
		}
	}
	
	void cmul();
	void cmul()
	{
		if (executing) {
			push( pop() * pop() );
		} else {
			cpush( builder->CreateBinOp( llvm::Instruction::Mul, cpop(), cpop() ) );
		}
	}
	
	void find();
	void find()
	{
		intptr_t len = pop();
		char *str = (char *)pop();
		
		std::string name( str, str + len );
		auto it = dictionary.find( name );
		if (it == dictionary.end()) {
			push( 0 );
		} else {
			push( (intptr_t)&it->second );
		}
	}
	
	void fword();
	void fword()
	{
		static char buf[32];
		std::string result = word();
		std::copy( result.begin(), result.end(), buf );
		push( (intptr_t)&buf );
		push( result.length() );
	}
	
	void toxt();
	void toxt()
	{
		Word *w = (Word *)pop();
		push( (intptr_t)w->function );
	}
	
	void execute();
	void execute()
	{
		llvm::Function *func = (llvm::Function *)pop();
		
		void (*funcPtr)() = (void (*)())engine->getPointerToFunction( func );
		funcPtr();
	}
	
	void tic();
	void tic()
	{
		fword();
		find();
		toxt();
		execute();
	}
}

struct IfBlocks {
	llvm::BasicBlock *endBlock;
	llvm::BasicBlock *trueBlock;
	llvm::BasicBlock *falseBlock;
};

std::vector<IfBlocks> ifstack;

extern "C" {
	
	void f_if();
	void f_if() 
	{
		if (executing) return;
		
		llvm::LLVMContext &c = llvm::getGlobalContext();
		
		llvm::Function *func = builder->GetInsertBlock()->getParent();
		
		IfBlocks blocks;
		blocks.trueBlock = llvm::BasicBlock::Create( c, "true", func  );
		blocks.falseBlock = llvm::BasicBlock::Create( c, "false", func );
		blocks.endBlock = llvm::BasicBlock::Create( c, "cont", func );
		
		llvm::IRBuilder<> tempBuilder( blocks.trueBlock );
		tempBuilder.CreateBr( blocks.endBlock );
		
		tempBuilder.SetInsertPoint( blocks.falseBlock );
		tempBuilder.CreateBr( blocks.endBlock );
		
		llvm::Value *cond = builder->CreateICmp( llvm::CmpInst::ICMP_NE, cpop(), builder->getInt64( 0 ) );
		builder->CreateCondBr( cond, blocks.trueBlock, blocks.falseBlock );
		
		builder->GetInsertBlock()->getInstList().erase( builder->saveIP().getPoint(), builder->GetInsertBlock()->end() );
		
		ifstack.push_back( blocks );
		
		builder->SetInsertPoint( blocks.trueBlock->getTerminator() );
	}
	
	void f_else();
	void f_else()
	{
		builder->SetInsertPoint( ifstack.back().falseBlock->getTerminator() );
	}
	
	void f_endif();
	void f_endif()
	{
		builder->SetInsertPoint( ifstack.back().endBlock );
		ifstack.pop_back();
		
		if (!ifstack.empty()) {
			auto ip = builder->CreateBr( ifstack.back().endBlock );
			builder->SetInsertPoint( ip );
		}
	}
}

int main (int argc, const char * argv[])
{
	llvm::InitializeNativeTarget();
	
	llvm::LLVMContext &Context = llvm::getGlobalContext();
	
	module = new llvm::Module( "forth", Context );

	install_word( "dump_stack" );
	install_word( "+", "cadd", true );
	install_word( "-", "csub", true );
	install_word( "*", "cmul", true );
	install_word( "swap" );
	install_word( "drop" );
	install_word( "dup", "cdup", true );
	install_word( ":", "docol" );
	install_word( ";", "done", true );
	install_word( "[", "lbrac", true );
	install_word( "]", "rbrac" );
	install_word( "char", "dochar" );
	install_word( "literal", true );
	install_word( "@", "load" );
	install_word( "!", "store" );
	install_word( "immediate", true );
	install_word( "'", "tic", false );
	install_word( "execute" );
	install_word( "find" );
	install_word( "word", "fword" );
	install_word( "if", "f_if", true );
	install_word( "else", "f_else", true );
	install_word( "then", "f_endif", true );
	install_word( "endif", "f_endif", true );
	install_word( ">xt", "toxt" );
	
	for (;;) interpret();
	
    return 0;
}

