#include <eosio/currency_plugin/currency_plugin.hpp>
#include <eosio/chain/eosio_contract.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/types.hpp>

#include <fc/io/json.hpp>
#include <fc/utf8.hpp>
#include <fc/variant.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/chrono.hpp>
#include <boost/signals2/connection.hpp>

#include <queue>
#include <map>

#include <curl/curl.h>
#include <jansson.h>

namespace fc { class variant; }

namespace eosio {

using chain::account_name;
using chain::action_name;
using chain::block_id_type;
using chain::permission_name;
using chain::transaction;
using chain::signed_transaction;
using chain::signed_block;
using chain::transaction_id_type;
using chain::packed_transaction;
using chain::mysql_database_exception;
using chain::signed_block_ptr;
using chain::account_object;
using chain::by_name;


static appbase::abstract_plugin& _currency_plugin = app().register_plugin<currency_plugin>();

class currency_plugin_impl {
public:
   currency_plugin_impl();
   ~currency_plugin_impl();
   void init();

   fc::optional<boost::signals2::scoped_connection> accepted_block_connection;
   fc::optional<boost::signals2::scoped_connection> irreversible_block_connection;

   void consume_blocks();

   void accepted_block( const chain::block_state_ptr& );
   void applied_irreversible_block(const chain::block_state_ptr&);
   void process_accepted_block( const chain::block_state_ptr& );
   void _process_accepted_block( const chain::block_state_ptr& );
   void process_irreversible_block(const chain::block_state_ptr&);
   void _process_irreversible_block(const chain::block_state_ptr&);


   template<typename Queue, typename Entry> void queue(Queue& queue, const Entry& e);

   size_t max_queue_size = 0;
   int queue_sleep_time = 0;
   std::deque<chain::block_state_ptr> block_state_queue;
   std::deque<chain::block_state_ptr> block_state_process_queue;
   std::deque<chain::block_state_ptr> irreversible_block_state_queue;
   std::deque<chain::block_state_ptr> irreversible_block_state_process_queue;
   std::mutex mtx;
   std::condition_variable condition;
   std::thread consume_thread;
   std::atomic_bool done{false};
   std::atomic_bool startup{true};

   std::atomic_bool start_block_reached{false};
   uint32_t start_block_num = 0;

   std::vector<std::string> symbol;
   std::string deposit_user;
   std::string withdraw_user;
   std::string token_contract;
   std::string curl_url;

   struct deposit_data {
       std::string user_id;
       std::string symbol;
       std::string balance;
       std::string from;
       uint32_t height;
       std::string txid;
       uint64_t business_id;
   };
   std::map<std::string, std::vector<deposit_data>> deposit_block_record;
   struct withdraw_data {
       std::string user_id;
       std::string symbol;
       std::string balance;
       std::string to;
       uint32_t height;
       std::string txid;
       uint64_t business_id;
   };
   std::map<std::string, std::vector<withdraw_data>> withdraw_block_record;
   void send_deposit_request(const deposit_data &d, int type);
   void send_withdraw_request(const withdraw_data &d, int type);
};

void currency_plugin_impl::init()
{
    consume_thread = std::thread([this] { consume_blocks(); });
}

static size_t post_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    char *reply = (char *)userdata;
    strncat(reply, ptr, size*nmemb);
    return size * nmemb;
}

void currency_plugin_impl::send_deposit_request(const currency_plugin_impl::deposit_data &d, int type)
{
    json_t *reply = NULL;
    json_t *error  = NULL;
    json_t *result = NULL;

    json_t *request = json_object();
    json_object_set_new(request, "method", json_string("balance.incoming"));
    json_t *params = json_array();
    json_array_append_new(params, json_integer(std::stoull(d.user_id)));
    json_array_append_new(params, json_string(d.symbol.c_str()));
    json_array_append_new(params, json_string("deposit"));
    json_array_append_new(params, json_integer(d.business_id));
    json_array_append_new(params, json_string(d.balance.c_str()));
    json_t *detail = json_object();
    json_object_set_new(detail, "height", json_integer(d.height));
    json_object_set_new(detail, "from", json_string(d.from.c_str()));
    json_object_set_new(detail, "txid", json_string(d.txid.c_str()));
    json_array_append_new(params, detail);
    json_array_append_new(params, json_integer(type));
    json_object_set_new(request, "params", params);
    json_object_set_new(request, "id", json_integer(time(NULL)));
    char *request_data = json_dumps(request, 0);
    // json_decref(params);
    json_decref(request);

    CURL *curl = curl_easy_init();
    if (!curl) {
        elog("curl_easy_init failed.");
        return;
    }
    char reply_str[10240] = "";

    struct curl_slist *chunk = NULL;
    chunk = curl_slist_append(chunk, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_URL, curl_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, post_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply_str);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)(1000));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_data);

    ilog("curl reqeust: ${s}", ("s", request_data));
    CURLcode ret = curl_easy_perform(curl);
    char *result_data = NULL;
    if (ret != CURLE_OK) {
        elog("curl curl_easy_perform fail: ${s}", ("s", curl_easy_strerror(ret)));
        goto cleanup;
    }

    reply = json_loads(reply_str, 0, NULL);
    if (reply == NULL)
        goto cleanup;
    error = json_object_get(reply, "error");
    if (!json_is_null(error)) {
        elog("curl user ${u} deposit from ${f} fail: ${s}", ("u", d.user_id)("f", d.from)("s", reply_str));
        goto cleanup;
    }
    result = json_object_get(reply, "result");
    result_data = json_dumps(result, 0);
    ilog("curl user ${u} deposit from ${f} succeed. ${s}", ("u", d.user_id)("f", d.from)("s", result_data));
    json_incref(result);

cleanup:
    free(request_data);
    // if (reply_str != NULL)
        // free(reply_str);
    curl_easy_cleanup(curl);
    curl_slist_free_all(chunk);
    if (reply)
        json_decref(reply);
}


void currency_plugin_impl::send_withdraw_request(const currency_plugin_impl::withdraw_data &d, int type)
{
    json_t *reply = NULL;
    json_t *error  = NULL;
    json_t *result = NULL;

    json_t *request = json_object();
    json_object_set_new(request, "method", json_string("balance.withdrawing"));
    json_t *params = json_array();
    json_array_append_new(params, json_integer(std::stoull(d.user_id)));
    json_array_append_new(params, json_string(d.symbol.c_str()));
    json_array_append_new(params, json_string("withdraw"));
    json_array_append_new(params, json_integer(d.business_id));
    json_array_append_new(params, json_string(d.balance.c_str()));
    json_t *detail = json_object();
    json_object_set_new(detail, "height", json_integer(d.height));
    json_object_set_new(detail, "to", json_string(d.to.c_str()));
    json_object_set_new(detail, "txid", json_string(d.txid.c_str()));
    json_array_append_new(params, detail);
    json_array_append_new(params, json_integer(type));
    json_object_set_new(request, "params", params);
    json_object_set_new(request, "id", json_integer(time(NULL)));
    char *request_data = json_dumps(request, 0);
    // json_decref(params);
    json_decref(request);

    CURL *curl = curl_easy_init();
    if (!curl) {
        elog("curl_easy_init failed.");
        return;
    }
    char reply_str[10240] = "";

    struct curl_slist *chunk = NULL;
    chunk = curl_slist_append(chunk, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_URL, curl_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, post_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply_str);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)(1000));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_data);

    ilog("curl reqeust: ${s}", ("s", request_data));
    CURLcode ret = curl_easy_perform(curl);
    char *result_data = NULL;
    if (ret != CURLE_OK) {
        elog("curl curl_easy_perform fail: ${s}", ("s", curl_easy_strerror(ret)));
        goto cleanup;
    }

    reply = json_loads(reply_str, 0, NULL);
    if (reply == NULL)
        goto cleanup;
    error = json_object_get(reply, "error");
    if (!json_is_null(error)) {
        elog("curl user ${u} deposit to ${f} fail: ${s}", ("u", d.user_id)("f", d.to)("s", reply_str));
        goto cleanup;
    }
    result = json_object_get(reply, "result");
    result_data = json_dumps(result, 0);
    ilog("curl user ${u} deposit to ${f} succeed. ${s}", ("u", d.user_id)("f", d.to)("s", result_data));
    json_incref(result);

cleanup:
    free(request_data);
    // if (reply_str != NULL)
        // free(reply_str);
    curl_easy_cleanup(curl);
    curl_slist_free_all(chunk);
    if (reply)
        json_decref(reply);
}

template<typename Queue, typename Entry>
void currency_plugin_impl::queue( Queue& queue, const Entry& e ) {
   std::unique_lock<std::mutex> lock( mtx );
   auto queue_size = queue.size();
   if( queue_size > max_queue_size ) {
      lock.unlock();
      condition.notify_one();
      queue_sleep_time += 10;
      if( queue_sleep_time > 1000 )
         wlog("queue size: ${q}", ("q", queue_size));
      std::this_thread::sleep_for( std::chrono::milliseconds( queue_sleep_time ));
      lock.lock();
   } else {
      queue_sleep_time -= 10;
      if( queue_sleep_time < 0 ) queue_sleep_time = 0;
   }
   queue.emplace_back( e );
   lock.unlock();
   condition.notify_one();
}

void currency_plugin_impl::applied_irreversible_block( const chain::block_state_ptr& bs ) {
   try {
       queue(irreversible_block_state_queue, bs);
   } catch (fc::exception& e) {
      elog("FC Exception while applied_irreversible_block ${e}", ("e", e.to_string()));
   } catch (std::exception& e) {
      elog("STD Exception while applied_irreversible_block ${e}", ("e", e.what()));
   } catch (...) {
      elog("Unknown exception while applied_irreversible_block");
   }
}

void currency_plugin_impl::accepted_block( const chain::block_state_ptr& bs ) {
   try {
      if( !start_block_reached ) {
         if( bs->block_num >= start_block_num ) {
            start_block_reached = true;
         }
      }
      queue( block_state_queue, bs );
   } catch (fc::exception& e) {
      elog("FC Exception while accepted_block ${e}", ("e", e.to_string()));
   } catch (std::exception& e) {
      elog("STD Exception while accepted_block ${e}", ("e", e.what()));
   } catch (...) {
      elog("Unknown exception while accepted_block");
   }
}

void currency_plugin_impl::consume_blocks() {
   try {
      while (true) {
         std::unique_lock<std::mutex> lock(mtx);
         while ( block_state_queue.empty() &&
                 irreversible_block_state_queue.empty() &&
                 !done ) {
            condition.wait(lock);
         }

         size_t block_state_size = block_state_queue.size();
         if (block_state_size > 0) {
            block_state_process_queue = move(block_state_queue);
            block_state_queue.clear();
         }
         size_t irreversible_block_size = irreversible_block_state_queue.size();
         if (irreversible_block_size > 0) {
            irreversible_block_state_process_queue = move(irreversible_block_state_queue);
            irreversible_block_state_queue.clear();
         }

         lock.unlock();

         if (done) {
            ilog("draining queue, size: ${q}", ("q", block_state_size + irreversible_block_size));
         }

         // process blocks
         auto start_time = fc::time_point::now();
         auto size = block_state_process_queue.size();
         while (!block_state_process_queue.empty()) {
            const auto& bs = block_state_process_queue.front();
            process_accepted_block( bs );
            block_state_process_queue.pop_front();
         }
         auto time = fc::time_point::now() - start_time;
         auto per = size > 0 ? time.count()/size : 0;
         if( time > fc::microseconds(500000) ) // reduce logging, .5 secs
            ilog( "process_accepted_block,       time per: ${p}, size: ${s}, time: ${t}", ("s", size)("t", time)("p", per) );

         // process irreversible blocks
         start_time = fc::time_point::now();
         size = irreversible_block_state_process_queue.size();
         while (!irreversible_block_state_process_queue.empty()) {
            const auto& bs = irreversible_block_state_process_queue.front();
            process_irreversible_block(bs);
            irreversible_block_state_process_queue.pop_front();
         }
         time = fc::time_point::now() - start_time;
         per = size > 0 ? time.count()/size : 0;
         if( time > fc::microseconds(500000) ) // reduce logging, .5 secs
            ilog( "process_irreversible_block,   time per: ${p}, size: ${s}, time: ${t}", ("s", size)("t", time)("p", per) );

         if( block_state_size == 0 &&
             irreversible_block_size == 0 &&
             done ) {
            break;
         }
      }
      ilog("currency_plugin consume thread shutdown gracefully");
   } catch (fc::exception& e) {
      elog("FC Exception while consuming block ${e}", ("e", e.to_string()));
   } catch (std::exception& e) {
      elog("STD Exception while consuming block ${e}", ("e", e.what()));
   } catch (...) {
      elog("Unknown exception while consuming block");
   }
}

void currency_plugin_impl::process_irreversible_block(const chain::block_state_ptr& bs) {
  try {
     if( start_block_reached ) {
        _process_irreversible_block(bs);
     }
  } catch (fc::exception& e) {
     elog("FC Exception while processing irreversible block: ${e}", ("e", e.to_detail_string()));
  } catch (std::exception& e) {
     elog("STD Exception while processing irreversible block: ${e}", ("e", e.what()));
  } catch (...) {
     elog("Unknown exception while processing irreversible block");
  }
}

void currency_plugin_impl::process_accepted_block( const chain::block_state_ptr& bs ) {
   try {
      if (start_block_reached) {
         _process_accepted_block(bs);
      }
   } catch (fc::exception& e) {
      elog("FC Exception while processing accepted block trace ${e}", ("e", e.to_string()));
   } catch (std::exception& e) {
      elog("STD Exception while processing accepted block trace ${e}", ("e", e.what()));
   } catch (...) {
      elog("Unknown exception while processing accepted block trace");
   }
}

struct resolver_factory {
   static auto make(const fc::microseconds& max_serialization_time) {
      return [max_serialization_time](const account_name &name) -> optional<abi_serializer> {
         const auto* accnt =  app().find_plugin<chain_plugin>()->chain().db().template find<account_object, by_name>(name);
         if (accnt != nullptr) {
            abi_def abi;
            if (abi_serializer::to_abi(accnt->abi, abi)) {
               return abi_serializer(abi, max_serialization_time);
            }
         }

         return optional<abi_serializer>();
      };
   }
};

auto make_resolver(const fc::microseconds& max_serialization_time) {
   return resolver_factory::make(max_serialization_time);
}

void currency_plugin_impl::_process_accepted_block( const chain::block_state_ptr& bs ) {
   signed_block_ptr &block = bs->block;
   fc::variant pretty_output;
   static const fc::microseconds abi_serializer_max_time = fc::seconds(10);
   uint32_t new_height = block->block_num();

   abi_serializer::to_variant(*block, pretty_output, make_resolver(abi_serializer_max_time), abi_serializer_max_time);
   const fc::variant &transactions = pretty_output["transactions"];
   try
   {
   for (size_t i = 0; i < transactions.size(); ++i) {
      const fc::variant &trx = transactions[i]["trx"];
      if (!trx.is_object())
        continue;
      const fc::variant &actions = trx["transaction"]["actions"];
      for (size_t j = 0; j < actions.size(); ++j) {
         const fc::variant &action = actions[j];
         if (action["account"].as_string() == token_contract && action["name"].as_string() == "transfer") {
            const fc::variant &data = action["data"];
            asset quantity = asset::from_string(data["quantity"].as_string());
            size_t pos;
            for (pos = 0; pos<symbol.size(); ++pos) {
                if (symbol[pos] == quantity.symbol_name()) {
                    break;
                }
            }
            if ( pos < symbol.size()) {
                std::string id = block->id().str();
                if (data["to"].as_string() == deposit_user) {
                    std::string s = data["quantity"].as_string();
                    std::string balance = s.substr(0, s.find(' '));
                    deposit_data d;
                    d.user_id = data["memo"].as_string();
                    d.symbol = symbol[pos];
                    d.balance = balance;
                    d.from = data["from"].as_string();
                    d.height = new_height;
                    d.txid = trx["id"].as_string();
                    struct timeval tv;
                    gettimeofday(&tv, NULL);
                    d.business_id = tv.tv_sec * 1000000 + tv.tv_usec;
                    send_deposit_request(d, 1);
                    deposit_block_record[id].push_back(d);
                } else if (data["from"].as_string() == withdraw_user) {
                    std::string s = data["quantity"].as_string();
                    std::string balance = s.substr(0, s.find(' '));
                    withdraw_data d;
                    vector<string> memos;
                    std::string memo = data["memo"].as_string();
                    boost::split(memos, memo, boost::is_any_of(" \t,"));
                    if (memos.size() != 2) {
                        elog("withdraw: ${s}", ("s", memo));
                        return;
                    }
                    d.user_id = memos[0];
                    d.symbol = symbol[pos];
                    d.balance = balance;
                    d.to = data["to"].as_string();
                    d.height = new_height;
                    d.txid = trx["id"].as_string();
                    // struct timeval tv;
                    // gettimeofday(&tv, NULL);
                    // d.business_id = tv.tv_sec * 1000000 + tv.tv_usec;
                    d.business_id = std::stoull(memos[1]);
                    send_withdraw_request(d, 1);
                    withdraw_block_record[id].push_back(d);
                }
            }
         }
      }
   }
   }
   catch (...)
   {
      ilog("error _process_accepted_block");
   }
}

void currency_plugin_impl::_process_irreversible_block(const chain::block_state_ptr& s) {
   try
   {
      std::string block_num = fc::to_string(s->block_num);
      std::string id = s->id.str();
      {
          auto ite = deposit_block_record.find(id);
          if (ite != deposit_block_record.end())
          {
              for (auto inner_ite = ite->second.begin(); inner_ite != ite->second.end(); ++inner_ite)
              {
                  send_deposit_request(*inner_ite, 2);
              }
              deposit_block_record.erase(id);
          }
      }
      {
          auto ite = withdraw_block_record.find(id);
          if (ite != withdraw_block_record.end())
          {
              for (auto inner_ite = ite->second.begin(); inner_ite != ite->second.end(); ++inner_ite)
              {
                  send_withdraw_request(*inner_ite, 2);
              }
              withdraw_block_record.erase(id);
          }
      }
   }
   catch (...)
   {
      ilog("error _process_irreversible_block");
   }
}


currency_plugin_impl::currency_plugin_impl()
{ } 

currency_plugin_impl::~currency_plugin_impl() {
   if (!startup) {
      try {
         ilog( "currency_plugin shutdown in process please be patient this can take a few minutes" );
         done = true;
         condition.notify_one();

         consume_thread.join();

      } catch( std::exception& e ) {
         elog( "Exception on currency_plugin shutdown of consume thread: ${e}", ("e", e.what()));
      }
   }

}


////////////
// currency_plugin
////////////

currency_plugin::currency_plugin()
:my(new currency_plugin_impl)
{
}

currency_plugin::~currency_plugin()
{
}

void currency_plugin::set_program_options(options_description& cli, options_description& cfg)
{
   cfg.add_options()
         ("token-contract", bpo::value<std::string>()->default_value( "eosio.token" ), "Official token contract")
         ("currency-queue-size", bpo::value<uint32_t>()->default_value(1024),
         "The target queue size between nodeos and currency plugin process.")
         ("currency-symbol",  bpo::value< vector<string> >()->composing(), "Symbol")
         ("currency-deposit-user", bpo::value<std::string>(), "Official cash deposit user")
         ("currency-withdraw-user", bpo::value<std::string>(), "Official cash withdrawal user")
         ("currency-block-start", bpo::value<uint32_t>()->default_value(0),
         "If specified then only abi data pushed to currency plugin until specified block is reached.")
         ("currency-curl-url", bpo::value<std::string>(), "Exchange http address")
         ;
}

void currency_plugin::plugin_initialize(const variables_map& options)
{
   try {
      ilog( "initializing currency_plugin" );
      if( options.count( "token-contract" )) {
         my->token_contract = options.at( "token-contract" ).as<std::string>();
      } else {
         wlog( "token-contract options missing" );
         return;
      }

      if( options.count( "currency-queue-size" )) {
         my->max_queue_size = options.at( "currency-queue-size" ).as<uint32_t>();
      }
      if( options.count( "currency-block-start" )) {
         my->start_block_num = options.at( "currency-block-start" ).as<uint32_t>();
         if( my->start_block_num == 0 ) {
            my->start_block_reached = true;
         }
      }
      if( options.count( "currency-symbol" )) {
         my->symbol = options.at( "currency-symbol" ).as<std::vector<std::string>>();
      } else {
         wlog( "currency-symbol options missing" );
         return;
      }

      if( options.count( "currency-deposit-user" )) {
         my->deposit_user = options.at( "currency-deposit-user" ).as<std::string>();
      } else {
         wlog( "currency-deposit-user options missing" );
         return;
      }
      if( options.count( "currency-withdraw-user" )) {
         my->withdraw_user = options.at( "currency-withdraw-user" ).as<std::string>();
      } else {
         wlog( "currency-withdraw-user options missing" );
         return;
      }
      if( options.count( "currency-curl-url" )) {
         my->curl_url = options.at( "currency-curl-url" ).as<std::string>();
      } else {
         wlog( "currency-curl-url options missing" );
         return;
      }
      my->init();
      my->startup = false;

      chain_plugin* chain_plug = app().find_plugin<chain_plugin>();
      EOS_ASSERT( chain_plug, chain::missing_chain_plugin_exception, ""  );
      auto& chain = chain_plug->chain();
      my->accepted_block_connection.emplace( chain.accepted_block.connect( [&]( const chain::block_state_ptr& bs ) {
         my->accepted_block( bs );
      } ));
      my->irreversible_block_connection.emplace(
            chain.irreversible_block.connect( [&]( const chain::block_state_ptr& bs ) {
               my->applied_irreversible_block( bs );
            } ));
   } FC_LOG_AND_RETHROW()
}

void currency_plugin::plugin_startup()
{
}

void currency_plugin::plugin_shutdown()
{
   my->accepted_block_connection.reset();
   my->irreversible_block_connection.reset();

   my.reset();
}

} // namespace eosio
