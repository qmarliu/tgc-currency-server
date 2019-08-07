#pragma once

#include <eosio/chain_plugin/chain_plugin.hpp>
#include <appbase/application.hpp>
#include <memory>

#include <eosio/chain/account_object.hpp>

#include <appbase/application.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/symbol.hpp>

#include <fc/static_variant.hpp>
#include <fc/safe.hpp>
#include <regex>
#include <vector>
#include <pwd.h>

#include <boost/range/adaptor/transformed.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/algorithm/string/classification.hpp>

namespace eosio {

using currency_plugin_impl_ptr = std::shared_ptr<class currency_plugin_impl>;

class currency_plugin : public plugin<currency_plugin> {
public:
   APPBASE_PLUGIN_REQUIRES((chain_plugin))

   currency_plugin();
   virtual ~currency_plugin();

   virtual void set_program_options(options_description& cli, options_description& cfg) override;

   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();

private:

   currency_plugin_impl_ptr my;
};

}