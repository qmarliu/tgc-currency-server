#include <kbtdeposit.hpp>

ACTION kbtdeposit::transfer(name from, name to, asset quantity, std::string memo) {
   // eosio_assert( memo.size() == 7, "memo not 7 bytes" );
   eosio_assert( memo[0] != '0', "record cannot start with 0" );

//    symbol support_symbol("KBT", 4);
//    eosio_assert( quantity.symbol == support_symbol, "symbol not support" );

   if (from == _self) 
      eosio_assert( quantity.amount >= _userprecision.minimum_out, "Not meeting the minimum amount" );
   else if (to == _self)
      eosio_assert( quantity.amount >= _userprecision.minimum_in, "Not meeting the minimum amount" );
   else
      eosio_assert( false, "reject inline call." );

   uint64_t prikey = std::stoull(memo);


   if (to == _self) {
      auto existing = _balancesIndex.find(prikey);
      eosio_assert( existing != _balancesIndex.end(), "memo not match" );
      _balancesIndex.modify( existing, _self, [&]( auto& s ) {
         s.balance += quantity.amount;
      });
   }
}

ACTION kbtdeposit::insert(uint64_t memo)
{
   require_auth(get_self());
   // eosio_assert( memo >= 1000000 && memo <= 9999999, "memo not 7 bytes" );
   auto existing = _balancesIndex.find(memo);
   eosio_assert( existing == _balancesIndex.end(), "memo exist" );
   _balancesIndex.emplace( _self, [&]( auto& s ) {
       s.memo = memo;
       s.balance = 0;
   });
}

ACTION kbtdeposit::erase(uint64_t memo)
{
   require_auth(get_self());
   // eosio_assert( memo >= 1000000 && memo <= 9999999, "memo not 7 bytes" );
   auto existing = _balancesIndex.find(memo);
   eosio_assert( existing != _balancesIndex.end(), "memo not exist" );
   _balancesIndex.erase( *existing); 
}

ACTION kbtdeposit::setprecision(uint64_t minimum_in, uint64_t minimum_out)
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
            eosio::execute_action(eosio::name(receiver), eosio::name(code), &kbtdeposit::setprecision);
            break;
         case eosio::name("erase").value:
            eosio::execute_action(eosio::name(receiver), eosio::name(code), &kbtdeposit::erase);
            break;
         case eosio::name("insert").value:
            eosio::execute_action(eosio::name(receiver), eosio::name(code), &kbtdeposit::insert);
            break;
         case eosio::name("transfer").value:
            // eosio_assert( false, "reject inline call." );
            break;
         }
      }
      else if (code == name("eosio.token").value) {
         switch (action)
         {
         case eosio::name("transfer").value:
            eosio::execute_action(eosio::name(receiver), eosio::name(code), &kbtdeposit::transfer);
            break;
         }
      }
    //   else if (action == eosio::name("transfer").value) {
    //      eosio_assert( false, "reject inline call." );
    //   }
   }
}