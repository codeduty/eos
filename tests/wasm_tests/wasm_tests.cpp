#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/contracts/abi_serializer.hpp>
#include <asserter/asserter.wast.hpp>
#include <asserter/asserter.abi.hpp>

#include <test_api/test_api.wast.hpp>

#include <currency/currency.wast.hpp>
#include <currency/currency.abi.hpp>

#include <proxy/proxy.wast.hpp>
#include <proxy/proxy.abi.hpp>


#include <fc/variant_object.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::chain::contracts;
using namespace eosio::testing;
using namespace fc;

struct issue {
   static uint64_t get_scope(){ return N(currency); }
   static uint64_t get_name(){ return N(issue); }

   account_name to;
   asset        quantity;
};
FC_REFLECT( issue, (to)(quantity) )


struct assertdef {
   int8_t      condition;
   string      message;

   static scope_name get_scope() {
      return N(asserter);
   }

   static action_name get_name() {
      return N(procassert);
   }
};

FC_REFLECT(assertdef, (condition)(message));

struct provereset {
   static scope_name get_scope() {
      return N(asserter);
   }

   static action_name get_name() {
      return N(provereset);
   }
};

FC_REFLECT_EMPTY(provereset);

constexpr uint32_t DJBH(const char* cp)
{
   uint32_t hash = 5381;
   while (*cp)
      hash = 33 * hash ^ (unsigned char) *cp++;
   return hash;
}

constexpr uint64_t TEST_METHOD(const char* CLASS, const char *METHOD) {
   return ( (uint64_t(DJBH(CLASS))<<32) | uint32_t(DJBH(METHOD)) );
}


template<uint64_t NAME>
struct test_api_action {
   static scope_name get_scope() {
      return N(tester);
   }

   static action_name get_name() {
      return action_name(NAME);
   }
};
FC_REFLECT_TEMPLATE((uint64_t T), test_api_action<T>, BOOST_PP_SEQ_NIL);


BOOST_AUTO_TEST_SUITE(wasm_tests)

/**
 * Prove that action reading and assertions are working
 */
BOOST_FIXTURE_TEST_CASE( basic_test, tester ) try {
   produce_blocks(2);

   create_accounts( {N(asserter)}, asset::from_string("1000.0000 EOS") );
   transfer( N(inita), N(asserter), "10.0000 EOS", "memo" );
   produce_block();

   set_code(N(asserter), asserter_wast);
   produce_blocks(1);

   transaction_id_type no_assert_id;
   {
      signed_transaction trx;
      trx.actions.emplace_back( vector<permission_level>{{N(asserter),config::active_name}},
                                assertdef {1, "Should Not Assert!"} );

      set_tapos( trx );
      trx.sign( get_private_key( N(asserter), "active" ), chain_id_type() );
      auto result = control->push_transaction( trx );
      BOOST_CHECK_EQUAL(result.status, transaction_receipt::executed);
      BOOST_CHECK_EQUAL(result.action_traces.size(), 1);
      BOOST_CHECK_EQUAL(result.action_traces.at(0).receiver.to_string(),  name(N(asserter)).to_string() );
      BOOST_CHECK_EQUAL(result.action_traces.at(0).act.scope.to_string(), name(N(asserter)).to_string() );
      BOOST_CHECK_EQUAL(result.action_traces.at(0).act.name.to_string(),  name(N(procassert)).to_string() );
      BOOST_CHECK_EQUAL(result.action_traces.at(0).act.authorization.size(),  1 );
      BOOST_CHECK_EQUAL(result.action_traces.at(0).act.authorization.at(0).actor.to_string(),  name(N(asserter)).to_string() );
      BOOST_CHECK_EQUAL(result.action_traces.at(0).act.authorization.at(0).permission.to_string(),  name(config::active_name).to_string() );
      no_assert_id = trx.id();
   }

   produce_blocks(1);

   BOOST_REQUIRE_EQUAL(true, chain_has_transaction(no_assert_id));
   const auto& receipt = get_transaction_receipt(no_assert_id);
   BOOST_CHECK_EQUAL(transaction_receipt::executed, receipt.status);

   transaction_id_type yes_assert_id;
   {
      signed_transaction trx;
      trx.actions.emplace_back( vector<permission_level>{{N(asserter),config::active_name}},
                                assertdef {0, "Should Assert!"} );

      set_tapos( trx );
      trx.sign( get_private_key( N(asserter), "active" ), chain_id_type() );
      yes_assert_id = trx.id();

      BOOST_CHECK_THROW(control->push_transaction( trx ), fc::assert_exception);
   }

   produce_blocks(1);

   auto has_tx = chain_has_transaction(yes_assert_id);
   BOOST_REQUIRE_EQUAL(false, has_tx);

} FC_LOG_AND_RETHROW() /// basic_test

/**
 * Prove the modifications to global variables are wiped between runs
 */
BOOST_FIXTURE_TEST_CASE( prove_mem_reset, tester ) try {
   produce_blocks(2);

   create_accounts( {N(asserter)}, asset::from_string("1000.0000 EOS") );
   transfer( N(inita), N(asserter), "10.0000 EOS", "memo" );
   produce_block();

   set_code(N(asserter), asserter_wast);
   produce_blocks(1);

   // repeat the action multiple times, each time the action handler checks for the expected
   // default value then modifies the value which should not survive until the next invoction
   for (int i = 0; i < 5; i++) {
      signed_transaction trx;
      trx.actions.emplace_back( vector<permission_level>{{N(asserter),config::active_name}},
                                provereset {} );

      set_tapos( trx );
      trx.sign( get_private_key( N(asserter), "active" ), chain_id_type() );
      control->push_transaction( trx );
      produce_blocks(1);
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
      const auto& receipt = get_transaction_receipt(trx.id());
      BOOST_CHECK_EQUAL(transaction_receipt::executed, receipt.status);
   }

} FC_LOG_AND_RETHROW() /// prove_mem_reset

/**
 * Prove the modifications to global variables are wiped between runs
 */
BOOST_FIXTURE_TEST_CASE( abi_from_variant, tester ) try {
   produce_blocks(2);

   create_accounts( {N(asserter)}, asset::from_string("1000.0000 EOS") );
   transfer( N(inita), N(asserter), "10.0000 EOS", "memo" );
   produce_block();

   set_code(N(asserter), asserter_wast);
   set_abi(N(asserter), asserter_abi);
   produce_blocks(1);

   auto resolver = [&,this]( const account_name& name ) -> optional<abi_serializer> {
      try {
         const auto& accnt  = this->control->get_database().get<account_object,by_name>( name );
         abi_def abi;
         if (abi_serializer::to_abi(accnt.abi, abi)) {
            return abi_serializer(abi);
         }
         return optional<abi_serializer>();
      } FC_RETHROW_EXCEPTIONS(error, "Failed to find or parse ABI for ${name}", ("name", name))
   };

   variant pretty_trx = mutable_variant_object()
      ("actions", variants({
         mutable_variant_object()
            ("scope", "asserter")
            ("name", "procassert")
            ("authorization", variants({
               mutable_variant_object()
                  ("actor", "asserter")
                  ("permission", name(config::active_name).to_string())
            }))
            ("data", mutable_variant_object()
               ("condition", 1)
               ("message", "Should Not Assert!")
            )
         })
      );

   signed_transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, resolver);
   set_tapos( trx );
   trx.sign( get_private_key( N(asserter), "active" ), chain_id_type() );
   control->push_transaction( trx );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
   const auto& receipt = get_transaction_receipt(trx.id());
   BOOST_CHECK_EQUAL(transaction_receipt::executed, receipt.status);

} FC_LOG_AND_RETHROW() /// prove_mem_reset

struct assert_message_is {
   assert_message_is(string expected)
   :expected(expected)
   {}

   bool operator()( const fc::assert_exception& ex ) {
      auto message = ex.get_log().at(0).get_message();
      return boost::algorithm::ends_with(message, expected);
   }

   string expected;
};


BOOST_FIXTURE_TEST_CASE( test_generic_currency, tester ) try {
   produce_blocks(2);
   create_accounts( {N(currency), N(usera), N(userb)}, asset::from_string("1000.0000 EOS") );
   produce_blocks(2);
   set_code( N(currency), currency_wast );
   produce_blocks(2);


   {
      signed_transaction trx;
      trx.actions.emplace_back(vector<permission_level>{{N(currency), config::active_name}},
                               issue{ .to = N(usera), 
                                      .quantity = asset::from_string( "10.0000 CUR" )
                                    });

      set_tapos(trx);
      trx.sign(get_private_key(N(currency), "active"), chain_id_type());
      push_transaction(trx);
      produce_block();
   }

} FC_LOG_AND_RETHROW() /// test_api_bootstrap

BOOST_FIXTURE_TEST_CASE( test_api_bootstrap, tester ) try {
   produce_blocks(2);

   create_accounts( {N(tester)}, asset::from_string("1000.0000 EOS") );
   transfer( N(inita), N(tester), "10.0000 EOS", "memo" );
   produce_block();

   set_code(N(tester), test_api_wast);
   produce_blocks(1);

   // make sure asserts function as we are predicated on them
   {
      signed_transaction trx;
      trx.actions.emplace_back(vector<permission_level>{{N(tester), config::active_name}},
                               test_api_action<TEST_METHOD("test_action", "assert_false")> {});

      set_tapos(trx);
      trx.sign(get_private_key(N(tester), "active"), chain_id_type());
      BOOST_CHECK_EXCEPTION(control->push_transaction(trx), fc::assert_exception, assert_message_is("test_action::assert_false"));
      produce_block();

      BOOST_REQUIRE_EQUAL(false, chain_has_transaction(trx.id()));
   }

   {
      signed_transaction trx;
      trx.actions.emplace_back(vector<permission_level>{{N(tester), config::active_name}},
                               test_api_action<TEST_METHOD("test_action", "assert_true")> {});

      set_tapos(trx);
      trx.sign(get_private_key(N(tester), "active"), chain_id_type());
      control->push_transaction(trx);
      produce_block();

      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
      const auto& receipt = get_transaction_receipt(trx.id());
      BOOST_CHECK_EQUAL(transaction_receipt::executed, receipt.status);
   }
} FC_LOG_AND_RETHROW() /// test_api_bootstrap

BOOST_FIXTURE_TEST_CASE( test_currency, tester ) try {
   produce_blocks(2);

   create_accounts( {N(currency), N(alice), N(bob)}, asset::from_string("1000.0000 EOS") );
   transfer( N(inita), N(currency), "10.0000 EOS", "memo" );
   produce_block();

   set_code(N(currency), currency_wast);
   set_abi(N(currency), currency_abi);
   produce_blocks(1);

   const auto& accnt  = control->get_database().get<account_object,by_name>( N(currency) );
   abi_def abi;
   BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
   abi_serializer abi_ser(abi);

   // make a transfer from the contract to a user
   {
      signed_transaction trx;
      trx.write_scope = {N(currency),N(alice)};
      action transfer_act;
      transfer_act.scope = N(currency);
      transfer_act.name = N(transfer);
      transfer_act.authorization = vector<permission_level>{{N(currency), config::active_name}};
      transfer_act.data = abi_ser.variant_to_binary("transfer", mutable_variant_object()
         ("from", "currency")
         ("to",   "alice")
         ("quantity", 100)
      );
      trx.actions.emplace_back(std::move(transfer_act));

      set_tapos(trx);
      trx.sign(get_private_key(N(currency), "active"), chain_id_type());
      control->push_transaction(trx);
      produce_block();

      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
   }

   // Overspend!
   {
      signed_transaction trx;
      trx.write_scope = {N(alice),N(bob)};
      action transfer_act;
      transfer_act.scope = N(currency);
      transfer_act.name = N(transfer);
      transfer_act.authorization = vector<permission_level>{{N(alice), config::active_name}};
      transfer_act.data = abi_ser.variant_to_binary("transfer", mutable_variant_object()
         ("from", "alice")
         ("to",   "bob")
         ("quantity", 101)
      );
      trx.actions.emplace_back(std::move(transfer_act));

      set_tapos(trx);
      trx.sign(get_private_key(N(alice), "active"), chain_id_type());
      BOOST_CHECK_EXCEPTION(control->push_transaction(trx), fc::assert_exception, assert_message_is("integer underflow subtracting token balance"));
      produce_block();

      BOOST_REQUIRE_EQUAL(false, chain_has_transaction(trx.id()));
   }

   // Full spend
   {
      signed_transaction trx;
      trx.write_scope = {N(alice),N(bob)};
      action transfer_act;
      transfer_act.scope = N(currency);
      transfer_act.name = N(transfer);
      transfer_act.authorization = vector<permission_level>{{N(alice), config::active_name}};
      transfer_act.data = abi_ser.variant_to_binary("transfer", mutable_variant_object()
         ("from", "alice")
         ("to",   "bob")
         ("quantity", 100)
      );
      trx.actions.emplace_back(std::move(transfer_act));

      set_tapos(trx);
      trx.sign(get_private_key(N(alice), "active"), chain_id_type());
      control->push_transaction(trx);
      produce_block();

      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
   }

} FC_LOG_AND_RETHROW() /// test_currency

BOOST_FIXTURE_TEST_CASE( test_proxy, tester ) try {
   produce_blocks(2);

   create_account( N(proxy), asset::from_string("10000.0000 EOS") );
   create_accounts( {N(alice), N(bob)}, asset::from_string("1000.0000 EOS") );
   transfer( N(inita), N(alice), "10.0000 EOS", "memo" );
   produce_block();

   set_code(N(proxy), proxy_wast);
   set_abi(N(proxy), proxy_abi);
   produce_blocks(1);

   const auto& accnt  = control->get_database().get<account_object,by_name>( N(proxy) );
   abi_def abi;
   BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
   abi_serializer abi_ser(abi);

   // set up proxy owner
   {
      signed_transaction trx;
      trx.write_scope = {N(proxy)};
      action setowner_act;
      setowner_act.scope = N(proxy);
      setowner_act.name = N(setowner);
      setowner_act.authorization = vector<permission_level>{{N(proxy), config::active_name}};
      setowner_act.data = abi_ser.variant_to_binary("setowner", mutable_variant_object()
         ("owner", "bob")
         ("delay", 10)
      );
      trx.actions.emplace_back(std::move(setowner_act));

      set_tapos(trx);
      trx.sign(get_private_key(N(proxy), "active"), chain_id_type());
      control->push_transaction(trx);
      produce_block();
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
   }

   // for now wasm "time" is in seconds, so we have to truncate off any parts of a second that may have applied
   fc::time_point expected_delivery(fc::seconds(control->head_block_time().sec_since_epoch()) + fc::seconds(10));
   transfer(N(alice), N(proxy), "5.0000 EOS");

   while(control->head_block_time() < expected_delivery) {
      control->push_deferred_transactions(true);
      produce_block();
      BOOST_REQUIRE_EQUAL(get_balance( N(alice)), asset::from_string("5.0000 EOS").amount);
      BOOST_REQUIRE_EQUAL(get_balance( N(proxy)), asset::from_string("5.0000 EOS").amount);
      BOOST_REQUIRE_EQUAL(get_balance( N(bob)),   asset::from_string("0.0000 EOS").amount);
   }

   control->push_deferred_transactions(true);
   BOOST_REQUIRE_EQUAL(get_balance( N(alice)), asset::from_string("5.0000 EOS").amount);
   BOOST_REQUIRE_EQUAL(get_balance( N(proxy)), asset::from_string("0.0000 EOS").amount);
   BOOST_REQUIRE_EQUAL(get_balance( N(bob)),   asset::from_string("5.0000 EOS").amount);

} FC_LOG_AND_RETHROW() /// test_currency

BOOST_FIXTURE_TEST_CASE( test_deferred_failure, tester ) try {
   produce_blocks(2);

   create_accounts( {N(proxy), N(bob)}, asset::from_string("10000.0000 EOS") );
   create_account( N(alice), asset::from_string("1000.0000 EOS") );
   transfer( N(inita), N(alice), "10.0000 EOS", "memo" );
   produce_block();

   set_code(N(proxy), proxy_wast);
   set_abi(N(proxy), proxy_abi);
   set_code(N(bob), proxy_wast);
   produce_blocks(1);

   const auto& accnt  = control->get_database().get<account_object,by_name>( N(proxy) );
   abi_def abi;
   BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
   abi_serializer abi_ser(abi);

   // set up proxy owner
   {
      signed_transaction trx;
      trx.write_scope = {N(proxy)};
      action setowner_act;
      setowner_act.scope = N(proxy);
      setowner_act.name = N(setowner);
      setowner_act.authorization = vector<permission_level>{{N(proxy), config::active_name}};
      setowner_act.data = abi_ser.variant_to_binary("setowner", mutable_variant_object()
         ("owner", "bob")
         ("delay", 10)
      );
      trx.actions.emplace_back(std::move(setowner_act));

      set_tapos(trx);
      trx.sign(get_private_key(N(proxy), "active"), chain_id_type());
      control->push_transaction(trx);
      produce_block();
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
   }

   // for now wasm "time" is in seconds, so we have to truncate off any parts of a second that may have applied
   fc::time_point expected_delivery(fc::seconds(control->head_block_time().sec_since_epoch()) + fc::seconds(10));
   auto trace = transfer(N(alice), N(proxy), "5.0000 EOS");
   BOOST_REQUIRE_EQUAL(trace.deferred_transactions.size(), 1);
   auto deferred_id = trace.deferred_transactions.back().id();

   while(control->head_block_time() < expected_delivery) {
      control->push_deferred_transactions(true);
      produce_block();
      BOOST_REQUIRE_EQUAL(get_balance( N(alice)), asset::from_string("5.0000 EOS").amount);
      BOOST_REQUIRE_EQUAL(get_balance( N(proxy)), asset::from_string("5.0000 EOS").amount);
      BOOST_REQUIRE_EQUAL(get_balance( N(bob)),   asset::from_string("0.0000 EOS").amount);
      BOOST_REQUIRE_EQUAL(chain_has_transaction(deferred_id), false);
   }

   fc::time_point expected_redelivery(fc::seconds(control->head_block_time().sec_since_epoch()) + fc::seconds(10));
   control->push_deferred_transactions(true);
   produce_block();
   BOOST_REQUIRE_EQUAL(chain_has_transaction(deferred_id), true);
   BOOST_REQUIRE_EQUAL(get_transaction_receipt(deferred_id).status, transaction_receipt::soft_fail);

   // set up bob owner
   {
      signed_transaction trx;
      trx.write_scope = {N(bob)};
      action setowner_act;
      setowner_act.scope = N(bob);
      setowner_act.name = N(setowner);
      setowner_act.authorization = vector<permission_level>{{N(bob), config::active_name}};
      setowner_act.data = abi_ser.variant_to_binary("setowner", mutable_variant_object()
         ("owner", "alice")
         ("delay", 0)
      );
      trx.actions.emplace_back(std::move(setowner_act));

      set_tapos(trx);
      trx.sign(get_private_key(N(bob), "active"), chain_id_type());
      control->push_transaction(trx);
      produce_block();
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
   }

   while(control->head_block_time() < expected_redelivery) {
      control->push_deferred_transactions(true);
      produce_block();
      BOOST_REQUIRE_EQUAL(get_balance( N(alice)), asset::from_string("5.0000 EOS").amount);
      BOOST_REQUIRE_EQUAL(get_balance( N(proxy)), asset::from_string("5.0000 EOS").amount);
      BOOST_REQUIRE_EQUAL(get_balance( N(bob)),   asset::from_string("0.0000 EOS").amount);
   }

   control->push_deferred_transactions(true);
   BOOST_REQUIRE_EQUAL(get_balance( N(alice)), asset::from_string("5.0000 EOS").amount);
   BOOST_REQUIRE_EQUAL(get_balance( N(proxy)), asset::from_string("0.0000 EOS").amount);
   BOOST_REQUIRE_EQUAL(get_balance( N(bob)),   asset::from_string("5.0000 EOS").amount);

   control->push_deferred_transactions(true);

   BOOST_REQUIRE_EQUAL(get_balance( N(alice)), asset::from_string("10.0000 EOS").amount);
   BOOST_REQUIRE_EQUAL(get_balance( N(proxy)), asset::from_string("0.0000 EOS").amount);
   BOOST_REQUIRE_EQUAL(get_balance( N(bob)),   asset::from_string("0.0000 EOS").amount);

} FC_LOG_AND_RETHROW() /// test_currency


BOOST_AUTO_TEST_SUITE_END()
