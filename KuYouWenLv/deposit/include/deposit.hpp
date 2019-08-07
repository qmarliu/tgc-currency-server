#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/singleton.hpp>
using namespace eosio;

CONTRACT kbtdeposit : public eosio::contract {
public:
  using contract::contract;

  kbtdeposit(name receiver, name code, datastream<const char *> ds)
      : contract(receiver, code, ds), 
        _balancesIndex(_self, _self.value), 
        _precision(_self, _self.value) {
           _userprecision = _precision.get_or_default();
        }
  
  ~kbtdeposit() {
      _precision.set( _userprecision, _self );
  }

  ACTION transfer(name from,
                  name to,
                  asset  quantity,
                  std::string memo);
  ACTION insert(uint64_t memo);
  ACTION erase(uint64_t memo);
  ACTION setprecision(uint64_t minimum_in, uint64_t minimum_out);

private:

  TABLE userbalances {
     uint64_t memo;
     uint64_t balance;
     uint64_t primary_key() const { return memo; }
     uint64_t get_balance() const { return balance; }
  };

  TABLE precision {
     uint64_t minimum_in = 1000;
     uint64_t minimum_out = 1000000;
  };

  typedef eosio::multi_index<
      "balances"_n, userbalances,
      indexed_by<"bybalance"_n, const_mem_fun<userbalances, uint64_t,
                                           &userbalances::get_balance>>>
      balances_index;
   
   typedef eosio::singleton<"precision"_n, precision> userprecision_singleton;

   balances_index _balancesIndex;
   userprecision_singleton _precision;
   precision _userprecision;
};