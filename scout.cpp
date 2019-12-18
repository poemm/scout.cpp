

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include <vector>
#include <fstream>
#include <iostream>

// wabt stuff
#include "src/binary-reader.h"
#include "src/cast.h"
#include "src/error-formatter.h"
#include "src/feature.h"
#include "src/interp/binary-reader-interp.h"
#include "src/interp/interp.h"
#include "src/literal.h"
#include "src/option-parser.h"
#include "src/resolve-names.h"
#include "src/stream.h"
#include "src/validator.h"
#include "src/wast-lexer.h"
#include "src/wast-parser.h"

// yaml stuff
#include "yaml-cpp/eventhandler.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep


int verbose = 0;


using namespace wabt;
using namespace wabt::interp;







////////////////
// world storage
// key-value pairs, keys are a unique address (otherwise overwrite), values are Account
// perhaps this will eventually become a merklized structure which can be handled by a database

static std::map<std::array<uint8_t,32>, struct Account*> world_storage;







//////////
// account
// like eth1 acct, contains a wasm code

struct Account {
  // data
  std::array<uint8_t,32> address;
  std::vector<uint8_t> bytecode;	  // shouldn't change, can point to code in another Account
  std::vector<uint8_t> state_root;

  // these are accessed by host function during execution
  std::vector<uint8_t>* calldata;	  // this is set when this module is called, and made null after the call returns
  interp::Memory* module_memory = nullptr;// memory of the module, used for copying calldata etc, problem with recursive calls

  // constructor
  Account(std::array<uint8_t,32> address, std::vector<uint8_t> &bytecode, std::vector<uint8_t> &state_root);

  // execute this bytecode
  ExecResult exec(std::vector<uint8_t> &calldata);
};



// constructor
Account::Account(std::array<uint8_t,32> address, std::vector<uint8_t> &bytecode, std::vector<uint8_t> &state_root) {
  //if (verbose) printf("Accout::Account()\n");

  // get address
  for (int i=0;i<32;i++)
    this->address[i] = address[i];

  // get bytecode
  for(int i=0;i<bytecode.size();i++){
    this->bytecode.push_back(bytecode.data()[i]);
  }

  for(int i=0;i<32;i++){
    this->state_root.push_back(state_root.data()[i]);
  }
}


// execute on calldata
ExecResult Account::exec(std::vector<uint8_t> &calldata){
  if(verbose){
    printf("exec(");
    for(int i=0;i<calldata.size();i++)
      printf("%u ",calldata.data()[i]);
    printf(")\n");
  }

  // set up calldata to be copied from
  this->calldata = &calldata;

  // env is like Wasm store, plus handles exports
  interp::Environment env(Features{});

  // create a host module as the first module in this store
  interp::HostModule* hostModule = env.AppendHostModule("env");

  // host module's functions, can be called from Wasm
  hostModule->AppendFuncExport(
    "eth2_loadPreStateRoot",
    {{Type::I32}, {}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results ) {
      if(verbose) printf("called host func loadPreStateRoot\n");
      uint32_t offset = static_cast<uint32_t>(args[0].value.i32);
      // TODO: check if within bounds of memory
      uint8_t* pre_state_root = this->state_root.data();//host_memory->data.data();
      uint8_t* module_memory = (uint8_t*) this->module_memory->data.data();
      //memcpy(module_memory+offset, host_memory, 32);
      for(int i=0;i<32;i++){
	if (verbose) printf("%u %u  ", module_memory[offset+i], pre_state_root[i]);
        module_memory[offset+i] = pre_state_root[i];
      }
      return interp::ResultType::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "eth2_blockDataSize",
    {{}, {Type::I32}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results ) {
      if(verbose) printf("called host func blockDataSize\n");
      if(verbose) printf("calldata size is %lu\n",this->calldata->size());
      results[0].set_i32(this->calldata->size());
      return interp::ResultType::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "eth2_blockDataCopy",
    {{Type::I32, Type::I32, Type::I32}, {}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results ) {
      uint32_t memory_offset = static_cast<uint32_t>(args[0].value.i32);
      uint32_t calldata_offset = static_cast<uint32_t>(args[1].value.i32);
      uint32_t length = static_cast<uint32_t>(args[2].value.i32);
      if(verbose) printf("called host func blockDataCopy %u %u %u\n",memory_offset, calldata_offset, length);
      // TODO: check if within bounds of memory and calldata
      uint8_t* memory = (uint8_t*) this->module_memory->data.data()+memory_offset;
      uint8_t* calldata = this->calldata->data()+calldata_offset;
      //memcpy(module_memory+outputOffset, this->calldata->data()+inputOffset, length);
      for(int i=0;i<length;i++){
	if(verbose) printf("%u %u  ", memory[i], calldata[i]);
        memory[i] = calldata[i];
      }
      if(verbose) printf("\n");
      return interp::ResultType::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "eth2_savePostStateRoot",
    {{Type::I32}, {}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results ) {
      uint32_t offset = static_cast<uint32_t>(args[0].value.i32);
      if(verbose) printf("called host func savePostStateRoot %u\n",offset);
      // TODO: check if within bounds of memory
      //uint8_t* host_memory = (uint8_t*) this->state_root;//host_memory->data.data();
      uint8_t* state_root = this->state_root.data();//host_memory->data.data();
      uint8_t* module_memory = (uint8_t*) this->module_memory->data.data();
      //memcpy(state_root, module_memory+offset, 32);
      for(int i=0;i<32;i++){
	if(verbose) printf("%u %u  ", state_root[i],module_memory[offset+i]);
        state_root[i] = module_memory[offset+i];
      }
      if(verbose) printf("\n");
      return interp::ResultType::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "eth2_pushNewDeposit",
    {{Type::I32, Type::I32}, {}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results) {
      uint32_t offset = static_cast<uint32_t>(args[0].value.i32);
      uint32_t length = static_cast<uint32_t>(args[1].value.i32);
      if(verbose) printf("called host func pushNewDeposit %u %u\n", offset, length);
      // TODO
      return interp::ResultType::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "eth2_debugPrintMem",
    {{Type::I32, Type::I32}, {}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results) {
      uint32_t offset = static_cast<uint32_t>(args[0].value.i32);
      uint32_t length = static_cast<uint32_t>(args[1].value.i32);
      if(verbose) printf("called host func debugPrintMem %u %u\n", offset, length);
      uint8_t* module_memory = (uint8_t*) this->module_memory->data.data();
      for(int i=0;i<length;i++){
	if(verbose) printf("%u ", module_memory[i]);
      }
      if(verbose) printf("\n");
      return interp::ResultType::Ok;
    }
  );

/*
  hostModule->AppendFuncExport(
    "bignum_f1m_add",
    {{Type::I32, Type::I32, Type::I32}, {}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results) {
      if(verbose) printf("called host func bignum_f1m_add\n");
      uint32_t x = static_cast<uint32_t>(args[0].value.i32);
      uint32_t y = static_cast<uint32_t>(args[1].value.i32);
      uint32_t out = static_cast<uint32_t>(args[2].value.i32);
      uint8_t* module_memory = (uint8_t*) this->module_memory->data.data();
      uint64_t carry=0;
      #pragma unroll
      for (int i=0; i<256/64;i++){
        uint64_t temp = x[i]+y[i]+carry;
        carry = x[i]>=temp ? 1:0;
        out[i]=temp;
      }
      return interp::ResultType::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "bignum_f1m_sub",
    {{Type::I32, Type::I32, Type::I32}, {}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results) {
      if(verbose) printf("called host func bignum_f1m_add\n");
      uint32_t x = static_cast<uint32_t>(args[0].value.i32);
      uint32_t y = static_cast<uint32_t>(args[1].value.i32);
      uint32_t out = static_cast<uint32_t>(args[2].value.i32);
      uint8_t* module_memory = (uint8_t*) this->module_memory->data.data();
      uint64_t carry=0;
      #pragma unroll
      for (int i=0; i<256/64;i++){
        uint64_t temp = x[i]-carry;
        out[i] = temp-y[i];
        carry = (temp<y[i] || x[i]<carry) ? 1:0;
      }
      return interp::ResultType::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "bignum_f1m_mul",
    {{Type::I32, Type::I32, Type::I32}, {}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results) {
      if(verbose) printf("called host func bignum_f1m_add\n");
      uint32_t x = static_cast<uint32_t>(args[0].value.i32);
      uint32_t y = static_cast<uint32_t>(args[1].value.i32);
      uint32_t out = static_cast<uint32_t>(args[2].value.i32);
      uint8_t* module_memory = (uint8_t*) this->module_memory->data.data();
      uint64_t* w = out;
      for (int i=0; i<2*256/64; i++)
        w[i]=0;
      for (int i=0; i<256/64; i++){
        uint64_t c = 0;
        for (int j=0; j<256/64; j++){
          __uint128_t uv = (__uint128_t)w[i+j] + (__uint128_t)x[j]*y[i];
          uv += c;
          __uint128_t u = uv >> 64;
          uint64_t v = uv;
          w[i+j] = v;
          c = u;
        }
        w[i+256/64] = c;
      }
      return interp::ResultType::Ok;
    }
  );


  hostModule->AppendFuncExport(
    "bignum_f1m_fromMontgomery",
    {{Type::I32, Type::I32}, {}},
    [&]( const interp::HostFunc*, const interp::FuncSignature*, 
                 const interp::TypedValues& args, interp::TypedValues& results) {
      if(verbose) printf("called host func bignum_f1m_add\n");
      uint32_t x = static_cast<uint32_t>(args[0].value.i32);
      uint32_t out = static_cast<uint32_t>(args[2].value.i32);
      uint8_t* module_memory = (uint8_t*) this->module_memory->data.data();
      uint64_t w[256/64*2];
      for (int i=0; i< 2*256/64; i++)
        w[i]=0;
      for (int i=0; i<256/64; i++){
        __uint128_t uv = (__uint128_t)w[2*i]+(__uint128_t)x[i]*x[i];
        __uint128_t u = uv >> 64;
        uint64_t v = uv;
        w[2*i] = v;
        __uint128_t c = u;
        for (int j=i+1; j<256/64; j++){
          uv = (__uint128_t)w[i+j]+2*(__uint128_t)x[j]*x[i]+c;
          u = uv >> 64;
          v = uv;
          w[i+j] = v;
          c = u;
        }
        w[i+256/64] = u;
      }
      for (int i=0; i< 2*256/64; i++)
        out[i]=w[i];
      return interp::ResultType::Ok;
    }
  );
*/

  // instantiate this bytecode in this env
  DefinedModule* module = nullptr;
  Errors errors;
  wabt::Result readresult = ReadBinaryInterp(&env, this->bytecode.data(), this->bytecode.size(),
                            ReadBinaryOptions{Features{}, nullptr, false, true, true},
                            &errors, &module);
  if (verbose){
    if(errors.size()){
      printf("found %lu errors:\n",errors.size());
      for (auto it = errors.begin(); it != errors.end(); ++it) {
        std::cout<< "error: "<< it->message << std::endl;
      }
    }
  }

  // get most recent memory, assuming this module is required to have a mem
  //if (verbose){ printf("num memories: %u\n",env.GetMemoryCount()); }
  this->module_memory = env.GetMemory(0);

  // get executor of main
  interp::Export* export_main = module->GetExport("main");
  interp::Executor executor( &env, nullptr, interp::Thread::Options{} );
  ExecResult execResult = executor.Initialize(module); // this finishes module instantiation

  // exec
  ExecResult result = executor.RunExport( export_main, interp::TypedValues{} );
  if (verbose){
    printf("done executing\n");
    if(!result.ok()){
      printf("Result not ok. Error:\n");	
      std::cout<< ResultToString(result.result)<<std::endl;
    }
    printf("%lu values returned\n",result.values.size());
    if (result.values.size()){
      printf("returned values:\n");
      for (auto it: result.values)
        std::cout<< TypedValueToString(it)<<std::endl;
    }
  }

  return result;
}







int parse_scout_yaml(
        std::string yaml_filename,
        std::vector<std::string> &filenames,
        std::vector< std::pair< uint32_t, std::vector<uint8_t> > > &shard_blocks, 
	std::vector< std::vector<uint8_t> > &prestates, 
        std::vector< std::vector<uint8_t> > &poststates){

  // parse yaml file
  YAML::Node yaml = YAML::LoadFile(yaml_filename);
  if (verbose) std::cout << yaml;

  // get filenames
  YAML::Node yaml_execution_scripts = yaml["beacon_state"]["execution_scripts"];
  for (std::size_t i=0;i<yaml_execution_scripts.size();i++) {
    filenames.push_back(yaml_execution_scripts[i].as<std::string>());
  }
  if (verbose){ printf("\nfilenames:\n"); for (std::size_t i=0;i<filenames.size();i++) { std::cout<<filenames[i]<<std::endl; } }

  // get prestates
  YAML::Node yaml_prestates = yaml["shard_pre_state"]["exec_env_states"];
  std::vector<std::string> prestates_hexstr;
  for (std::size_t i=0;i<yaml_prestates.size();i++) {
    prestates_hexstr.push_back(yaml_prestates[i].as<std::string>());
    prestates.push_back(std::vector<uint8_t>());
    for (int j = 0 ; j < prestates_hexstr[i].size() ; j+=2) {
      prestates[i].push_back(::strtol( prestates_hexstr[i].substr( j, 2 ).c_str(), 0, 16 ));
    }
  }
  if (verbose){ printf("\nprestates:\n"); for (std::size_t i=0;i<prestates_hexstr.size();i++) { std::cout<<prestates_hexstr[i]<<std::endl; } }

  // get shard blocks
  YAML::Node yaml_shard_blocks = yaml["shard_blocks"];
  std::vector<std::string> calldatas_str;
  for (std::size_t i=0;i<yaml_shard_blocks.size();i++) {
    YAML::Node env = yaml_shard_blocks[i]["env"];
    YAML::Node calldata_yaml = yaml_shard_blocks[i]["data"];
    std::vector<uint8_t> calldata;
    calldatas_str.push_back(calldata_yaml.as<std::string>());
    for (int j = 0 ; j < calldatas_str[i].size() ; j+=2) {
      calldata.push_back(::strtol( calldatas_str[i].substr( j, 2 ).c_str(), 0, 16 ));
    }
    shard_blocks.push_back(std::make_pair(env.as<uint32_t>(),calldata));
  }
  if (verbose){
    printf("\nblocks:\n"); 
    for (int i=0;i<shard_blocks.size();i++) {
      std::cout<<shard_blocks[i].first<<": ";
      for (int j=0;j<shard_blocks[i].second.size();j++) {
        //std::cout<<" "<<shard_blocks[i].second[j]<<std::endl; 
        printf(" %u",shard_blocks[i].second[j]); 
      }
      printf("\n"); 
    }
  }

  // get poststates
  YAML::Node yaml_poststates = yaml["shard_post_state"]["exec_env_states"];
  std::vector<std::string> poststates_hexstr;
  poststates.reserve(filenames.size());
  for (std::size_t i=0;i<yaml_poststates.size();i++) {
    poststates_hexstr.push_back(yaml_poststates[i].as<std::string>());
    poststates.push_back(std::vector<uint8_t>());
    for (int j = 0 ; j < poststates_hexstr[i].size() ; j+=2) {
      poststates[i].push_back(::strtol( poststates_hexstr[i].substr( j, 2 ).c_str(), 0, 16 ));
    }
  }
  if (verbose){ printf("\npoststates:\n"); for (std::size_t i=0;i<poststates_hexstr.size();i++) { std::cout<<poststates_hexstr[i]<<std::endl; } }

}


void print_files_prestates_blocks_poststates(
        std::vector<std::string> &filenames,
        std::vector< std::pair< uint32_t, std::vector<uint8_t> > > &shard_blocks, 
	std::vector< std::vector<uint8_t> > &prestates, 
        std::vector< std::vector<uint8_t> > &poststates){
  std::cout<<"\n\nprint_files_prestates_blocks_poststates()"<<std::endl;

  std::cout<<"\nfilenames:"<<std::endl;
  for (auto filename : filenames)
    std::cout<<filename<<std::endl;
    
  std::cout<<"\nprestates:"<<std::endl;
  printf("len prestates %lu\n",prestates.size());
  for (auto prestate : prestates){
    printf("len prestate %lu\n",prestate.size());
    for (auto s : prestate)
      printf("%u ",s);
    std::cout<<std::endl;
  }

  std::cout<<"\nshard_blocks:"<<std::endl;
  for (auto shard_block : shard_blocks){
    std::cout<<shard_block.first<<":  ";
    for (auto s : shard_block.second)
      printf("%u ",s);
    std::cout<<std::endl;
  }

  std::cout<<"\npoststates:"<<std::endl;
  for (auto poststate : poststates){
    for (auto s : poststate)
      printf("%u ",s);
    std::cout<<std::endl;
  }
  std::cout<<std::endl;
}


int main(int argc, char** argv) {

  //get all command-line args
  std::vector<std::string> args(argv, argv + argc);
  if (args.size()<2){
    printf("usage: ./scout.exec helloworld.yaml\n");
    return -1;
  }

  // parse scout-formatted yaml file to get wasm filenames, prestates, calldatas, and poststates
  std::vector<std::string> filenames;
  std::vector< std::pair< uint32_t, std::vector<uint8_t> > > shard_blocks; 
  std::vector< std::vector<uint8_t> > prestates;
  std::vector< std::vector<uint8_t> > poststates;
  parse_scout_yaml(args[1], filenames, shard_blocks, prestates, poststates);

  if(verbose) print_files_prestates_blocks_poststates(filenames, shard_blocks, prestates, poststates);

  if(filenames.size() != prestates.size() || prestates.size() != poststates.size())
    printf("ERROR: different numbers of files, prestates, or poststates\n");

  // get bytecode from each wasm file
  std::vector< std::vector<uint8_t> > bytecodes;
  for (int i=0; i<filenames.size(); i++){
    //if (verbose) std::cout<<"reading wasm file "<<filenames[i].c_str()<<std::endl;
    std::ifstream stream(filenames[i].c_str(), std::ios::in | std::ios::binary);
    bytecodes.push_back(std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>()));
  }

  // create each account with address, fill bytecode and prestate
  for (int i=0; i<filenames.size(); i++){
    std::array<uint8_t,32> address;
    for (int j=0; j<32; j++)
      address[j] = 0;
    *((uint32_t*)address.data())=i; // address is index for now
    // instantiate
    Account* account = new Account(address, bytecodes[i], prestates[i]);
    // register it globally
    world_storage[address]=account;
  }

  // execute each call
  for (int i=0; i<shard_blocks.size(); i++){
    std::array<uint8_t,32> address;
    for (int j=0; j<32; j++)
      address[j] = 0;
    *((uint32_t*)address.data())=shard_blocks[i].first; // address is index for now
    Account* account = world_storage[address];
    account->exec( shard_blocks[i].second );
  }

  // check post-states
  int errorFlag = 0;
  for (int i=0; i<poststates.size(); i++){
    std::array<uint8_t,32> address;
    for (int j=0; j<32; j++)
      address[j] = 0;
    *((uint32_t*)address.data())=i; // address is index for now
    // get account form global state
    Account* account = world_storage[address];
    // compare account state against expected poststate
    uint8_t* expected_poststate = poststates[i].data();
    for (int j=0; j<32; j++){
      if (account->state_root[j] != expected_poststate[j]){
        printf("error with poststate %u idx %u  %u!=%u\n", i, j, account->state_root[j], expected_poststate[j]);
	errorFlag = 1;
      }
    }
  }

  if (errorFlag==0)
    printf("passed\n");

  // clean up
  for (auto acct: world_storage)
    delete world_storage[acct.first];

  return 0;
}







/*
#    Copyright 2019 Paul Dworzanski et al.
#
#    This file is part of scout.cpp.
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
*/
