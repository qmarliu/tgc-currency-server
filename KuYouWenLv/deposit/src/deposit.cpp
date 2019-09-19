#include <deposit.hpp>

ACTION deposit::transfer(name from, name to, asset quantity, std::string memo) {
   if (from == _self) 
      check( quantity.amount >= _userprecision.minimum_out, "Not meeting the minimum amount" );
   else if (to == _self)
      check( quantity.amount >= _userprecision.minimum_in, "Not meeting the minimum amount" );
   else
      check( false, "reject inline call." );

   if (to == _self) {
       uint64_t prikey = std::stoull(memo);
       auto existing = _balancesIndex.find(prikey);
       check( existing != _balancesIndex.end(), "memo not match" );
       _balancesIndex.modify( existing, _self, [&]( auto& s ) {
           s.balance += quantity.amount;
       });
   }
}

ACTION deposit::insert(uint64_t memo)
{
   require_auth(get_self());
   auto existing = _balancesIndex.find(memo);
   check( existing == _balancesIndex.end(), "memo exist" );
   _balancesIndex.emplace( _self, [&]( auto& s ) {
       s.memo = memo;
       s.balance = 0;
   });
}

ACTION deposit::erase(uint64_t memo)
{
   require_auth(get_self());
   auto existing = _balancesIndex.find(memo);
   check( existing != _balancesIndex.end(), "memo not exist" );
   _balancesIndex.erase( *existing); 
}

ACTION deposit::setprecision(uint64_t minimum_in, uint64_t minimum_out)
{
   require_auth(get_self());
   _userprecision.minimum_in = minimum_in;
   _userprecision.minimum_out = minimum_out;
}
/**
 * *  The apply() methods must have C calling convention so that the blockchain can lookup and
 * *  call these methods.
 * */
// EOSIO_DISPATCH( usertransfer, (transfer)(insert))
 
extern "C" {
   /// The apply method implements the dispatch of events to this contract
   void apply( uint64_t receiver, uint64_t code, uint64_t action ) {
      if (receiver == code) {
         switch (action)
         {
         case eosio::name("setprecision").value:
            eosio::execute_action(eosio::name(receiver), eosio::name(code), &deposit::setprecision);
            break;
         case eosio::name("erase").value:
            eosio::execute_action(eosio::name(receiver), eosio::name(code), &deposit::erase);
            break;
         case eosio::name("insert").value:
            eosio::execute_action(eosio::name(receiver), eosio::name(code), &deposit::insert);
            break;
         case eosio::name("transfer").value:
            check( false, "reject inline call." );
            break;
         }
      }
      else if (code == name("evsio.token").value) {
         switch (action)
         {
         case eosio::name("transfer").value:
            eosio::execute_action(eosio::name(receiver), eosio::name(code), &deposit::transfer);
            break;
         }
      }
      else if (action == eosio::name("transfer").value) {
         check( false, "reject inline call." );
      }
   }
}